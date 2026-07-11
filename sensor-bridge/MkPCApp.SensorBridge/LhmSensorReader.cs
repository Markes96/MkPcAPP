using LibreHardwareMonitor.Hardware;

namespace MkPCApp.SensorBridge;

// Wraps LibreHardwareMonitorLib's Computer/IHardware/ISensor object model and
// produces one SharedSensorData snapshot per Update() call. Sensor names vary by
// vendor, so GPU load/VRAM sensors are matched by substring rather than exact name.
public sealed class LhmSensorReader : IDisposable
{
    private readonly Computer? _computer;

    // Computer.Open() has been observed to throw outright (rather than degrade
    // gracefully) when it can't get the access it wants — e.g. running
    // unelevated on some hardware. Since this constructor runs before the
    // per-tick try/catch in Program.cs even starts, an uncaught exception here
    // used to crash the bridge process immediately, before it ever published a
    // single snapshot — indistinguishable from the bridge not running at all.
    // Now it degrades to "no bridge sensors available" instead of crashing.
    public LhmSensorReader()
    {
        try
        {
            var computer = new Computer
            {
                IsCpuEnabled = true,
                IsGpuEnabled = true,
                IsMotherboardEnabled = true,
            };
            computer.Open();
            _computer = computer;
        }
        catch
        {
            _computer = null;
        }
    }

    public SharedSensorData ReadSnapshot()
    {
        var data = SharedSensorData.CreateEmpty();
        byte available = 0;

        if (_computer != null)
        {
            foreach (IHardware hardware in _computer.Hardware)
            {
                try
                {
                    hardware.Update();
                    foreach (IHardware sub in hardware.SubHardware)
                    {
                        sub.Update();
                    }

                    switch (hardware.HardwareType)
                    {
                        case HardwareType.Cpu:
                            available |= ReadCpu(hardware, ref data);
                            break;
                        case HardwareType.GpuNvidia:
                        case HardwareType.GpuAmd:
                        case HardwareType.GpuIntel:
                            available |= ReadGpu(hardware, ref data);
                            break;
                        case HardwareType.Motherboard:
                            available |= ReadFans(hardware, ref data);
                            break;
                    }
                }
                catch
                {
                    // One misbehaving hardware/sensor (driver quirk, vendor API
                    // hiccup) shouldn't take down the whole snapshot — the rest
                    // of the loop, and the next tick, still get a chance.
                }
            }
        }

        data.sensorsAvailable = available;
        data.bridgeElevated = (byte)(IsElevated() ? 1 : 0);
        data.timestampUnixMs = (ulong)DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        return data;
    }

    private static byte ReadCpu(IHardware hardware, ref SharedSensorData data)
    {
        data.SetCpuName(hardware.Name);
        byte bits = 0;
        ISensor? tempSensor = FindSensor(hardware.Sensors, SensorType.Temperature, "Package")
                              ?? FindFirst(hardware.Sensors, SensorType.Temperature);
        if (tempSensor?.Value is float temp)
        {
            data.cpuTempC = temp;
            bits |= (byte)SensorAvailableBits.CpuTemp;
        }
        return bits;
    }

    private static byte ReadGpu(IHardware hardware, ref SharedSensorData data)
    {
        data.SetGpuName(hardware.Name);
        byte bits = 0;

        ISensor? loadSensor = FindSensor(hardware.Sensors, SensorType.Load, "Core")
                              ?? FindFirst(hardware.Sensors, SensorType.Load);
        if (loadSensor?.Value is float load)
        {
            data.gpuUsagePercent = load;
            bits |= (byte)SensorAvailableBits.GpuUsage;
        }

        ISensor? tempSensor = FindSensor(hardware.Sensors, SensorType.Temperature, "Core")
                             ?? FindFirst(hardware.Sensors, SensorType.Temperature);
        if (tempSensor?.Value is float temp)
        {
            data.gpuTempC = temp;
            bits |= (byte)SensorAvailableBits.GpuTemp;
        }

        ISensor? vramUsed = FindSensor(hardware.Sensors, SensorType.SmallData, "Memory Used");
        ISensor? vramTotal = FindSensor(hardware.Sensors, SensorType.SmallData, "Memory Total");
        if (vramUsed?.Value is float used && vramTotal?.Value is float total)
        {
            data.vramUsedMB = used;
            data.vramTotalMB = total;
            bits |= (byte)SensorAvailableBits.Vram;
        }

        return bits;
    }

    private static byte ReadFans(IHardware motherboard, ref SharedSensorData data)
    {
        var fanSensors = new List<ISensor>();
        foreach (IHardware sub in motherboard.SubHardware)
        {
            fanSensors.AddRange(sub.Sensors.Where(s => s.SensorType == SensorType.Fan && s.Value is float f && f > 0));
        }

        int count = Math.Min(fanSensors.Count, SharedSensorData.MaxFans);
        for (int i = 0; i < count; i++)
        {
            data.SetFanRpm(i, fanSensors[i].Value ?? 0f);
            data.SetFanLabel(i, fanSensors[i].Name);
        }
        data.fanCount = (uint)count;

        return count > 0 ? (byte)SensorAvailableBits.Fans : (byte)0;
    }

    private static ISensor? FindSensor(IEnumerable<ISensor> sensors, SensorType type, string nameContains)
    {
        return sensors.FirstOrDefault(s => s.SensorType == type &&
            s.Name.Contains(nameContains, StringComparison.OrdinalIgnoreCase));
    }

    private static ISensor? FindFirst(IEnumerable<ISensor> sensors, SensorType type)
    {
        return sensors.FirstOrDefault(s => s.SensorType == type);
    }

    private static bool IsElevated()
    {
        using var identity = System.Security.Principal.WindowsIdentity.GetCurrent();
        var principal = new System.Security.Principal.WindowsPrincipal(identity);
        return principal.IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator);
    }

    public void Dispose()
    {
        _computer?.Close();
    }
}

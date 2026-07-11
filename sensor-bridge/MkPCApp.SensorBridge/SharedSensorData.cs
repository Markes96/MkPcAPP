using System.Runtime.InteropServices;

namespace MkPCApp.SensorBridge;

// MUST mirror src/ipc/SharedSensorData.h byte-for-byte. Bump StructVersion in both
// files on any layout change. Uses `fixed` inline buffers (not managed arrays) so the
// struct stays a true blittable value type — required to write it directly into the
// memory-mapped file via MemoryMappedViewAccessor.Write<T>/pointer access.
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct SharedSensorData
{
    public const uint StructVersion = 1;
    public const int MaxFans = 8;
    public const int FanLabelLength = 32;
    public const int NameLength = 64;

    public uint structVersion;
    public ulong writeSequence;
    public ulong timestampUnixMs;

    public float cpuTempC;
    public float gpuUsagePercent;
    public float gpuTempC;
    public float vramUsedMB;
    public float vramTotalMB;

    public uint fanCount;
    public fixed float fanRpm[MaxFans];
    public fixed byte fanLabel[MaxFans * FanLabelLength];

    public fixed byte gpuName[NameLength];
    public fixed byte cpuName[NameLength];

    public byte sensorsAvailable;
    public byte bridgeElevated;
    public byte reserved0;
    public byte reserved1;

    public static SharedSensorData CreateEmpty()
    {
        return new SharedSensorData { structVersion = StructVersion };
    }

    public void SetFanRpm(int index, float rpm)
    {
        fixed (float* basePtr = fanRpm)
        {
            basePtr[index] = rpm;
        }
    }

    public void SetFanLabel(int index, string label)
    {
        fixed (byte* basePtr = fanLabel)
        {
            WriteFixedAscii(basePtr + index * FanLabelLength, FanLabelLength, label);
        }
    }

    public void SetGpuName(string name)
    {
        fixed (byte* ptr = gpuName)
        {
            WriteFixedAscii(ptr, NameLength, name);
        }
    }

    public void SetCpuName(string name)
    {
        fixed (byte* ptr = cpuName)
        {
            WriteFixedAscii(ptr, NameLength, name);
        }
    }

    private static void WriteFixedAscii(byte* dest, int capacity, string value)
    {
        int count = Math.Min(value.Length, capacity - 1);
        for (int i = 0; i < count; i++)
        {
            char c = value[i];
            dest[i] = c <= 0x7F ? (byte)c : (byte)'?';
        }
        for (int i = count; i < capacity; i++)
        {
            dest[i] = 0;
        }
    }
}

[Flags]
public enum SensorAvailableBits : byte
{
    CpuTemp = 1 << 0,
    GpuUsage = 1 << 1,
    GpuTemp = 1 << 2,
    Vram = 1 << 3,
    Fans = 1 << 4,
}

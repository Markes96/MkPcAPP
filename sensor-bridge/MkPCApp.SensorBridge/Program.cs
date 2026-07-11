using System.Diagnostics;
using MkPCApp.SensorBridge;

// No UI, no console window (see OutputType=WinExe in the csproj). Reads hardware
// sensors once per second via LibreHardwareMonitorLib and publishes a snapshot to
// shared memory for MkPCApp.exe to read. Runs at low OS priority since this work
// is never latency-sensitive and must never contend with foreground apps.
try
{
    Process.GetCurrentProcess().PriorityClass = ProcessPriorityClass.Idle;
}
catch
{
    // Best-effort; not fatal if the OS refuses the priority change.
}

using var reader = new LhmSensorReader();
using var writer = new SharedMemoryWriter();

var cts = new CancellationTokenSource();
AppDomain.CurrentDomain.ProcessExit += (_, _) => cts.Cancel();

while (!cts.IsCancellationRequested)
{
    try
    {
        SharedSensorData snapshot = reader.ReadSnapshot();
        writer.Publish(snapshot);
    }
    catch
    {
        // A single failed sensor pass shouldn't kill the bridge; the native app's
        // supervisor already handles the case where the whole process dies.
    }

    try
    {
        await Task.Delay(TimeSpan.FromSeconds(1), cts.Token);
    }
    catch (TaskCanceledException)
    {
        break;
    }
}

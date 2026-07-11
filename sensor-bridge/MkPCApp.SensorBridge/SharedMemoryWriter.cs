using System.IO.MemoryMappedFiles;

namespace MkPCApp.SensorBridge;

// Owns the shared-memory segment the native app reads. Creates the mapping (the
// bridge is the writer/owner side) and publishes snapshots using a seqlock: bump
// writeSequence to odd before writing, write the struct, bump to even after — the
// native reader retries until it observes an even, unchanged sequence, so no
// cross-process mutex is needed for a once-per-second publish.
public sealed unsafe class SharedMemoryWriter : IDisposable
{
    private const string MappingName = "Local\\MkPCApp_SensorData_v1";

    private readonly MemoryMappedFile _mmf;
    private readonly MemoryMappedViewAccessor _accessor;
    private byte* _basePtr;
    private ulong _sequence;

    public SharedMemoryWriter()
    {
        int size = sizeof(SharedSensorData);
        // CreateOrOpen, not CreateNew: the native app keeps its own read handle
        // to this mapping open for its entire lifetime (see
        // SharedMemoryChannel::EnsureOpen), so the kernel object outlives any
        // single bridge process instance. If a previous bridge instance died
        // and got respawned (or a prior app instance was left running
        // minimized in the tray from an earlier test run) while that handle
        // was still open, CreateNew would throw "already exists" and silently
        // crash every subsequent bridge instance for the rest of the session —
        // which is exactly the "no sensor data at all" symptom this fixes.
        _mmf = MemoryMappedFile.CreateOrOpen(MappingName, size);
        _accessor = _mmf.CreateViewAccessor(0, size);
        _accessor.SafeMemoryMappedViewHandle.AcquirePointer(ref _basePtr);
    }

    public void Publish(in SharedSensorData data)
    {
        SharedSensorData* dest = (SharedSensorData*)_basePtr;

        _sequence++;
        dest->writeSequence = _sequence; // now odd: write in progress
        // Copy every field except writeSequence, which we manage here directly so
        // the "odd/even" invariant can't be clobbered by a stale value in `data`.
        SharedSensorData toWrite = data;
        toWrite.writeSequence = _sequence;
        *dest = toWrite;

        _sequence++;
        dest->writeSequence = _sequence; // now even: stable, safe to read
    }

    public void Dispose()
    {
        _accessor.SafeMemoryMappedViewHandle.ReleasePointer();
        _accessor.Dispose();
        _mmf.Dispose();
    }
}

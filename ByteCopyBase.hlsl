ByteAddressBuffer Source : register(t0);
RWByteAddressBuffer Destination : register(u0);

[numthreads(NUM_THREADS, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    const uint LoopCount = ITERATIONS;
    const uint OffsetPerThread = 16 * LoopCount;
    const uint BaseOffset = DTid.x * OffsetPerThread;
    [unroll]
    for (uint i = 0; i < LoopCount; i++)
    {
        const uint LoopOffset = 16 * i;
        uint4 Value = Source.Load4(BaseOffset + LoopOffset);
        Destination.Store4(BaseOffset + LoopOffset, Value);
    }
}
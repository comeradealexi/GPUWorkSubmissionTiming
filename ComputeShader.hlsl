ByteAddressBuffer BufferA;
RWByteAddressBuffer BufferB;

[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    BufferB.Store(DTid.r, BufferA.Load(DTid.r));
}
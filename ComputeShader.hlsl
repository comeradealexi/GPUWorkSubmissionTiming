ByteAddressBuffer Source : register(t0);
RWByteAddressBuffer Destination : register(u0);

[numthreads(256, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint4 Value = Source.Load4(DTid.x * 4);
    Destination.Store4(DTid.x * 4, Value);
}
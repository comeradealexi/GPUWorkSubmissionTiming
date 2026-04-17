ByteAddressBuffer Source : register(t0);
RWByteAddressBuffer Destination : register(u0);

[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    Destination.Store4(DTid.x * 4, Source.Load4(DTid.x * 4));
}
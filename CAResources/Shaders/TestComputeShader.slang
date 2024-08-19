

RWStructuredBuffer<float4> RWBuffer;
[shader("compute")]
[numthreads(1, 1, 1)]
void testGenerator0(uint3 threadId : SV_DispatchThreadID)
{
    RWBuffer[0] = float4(1, 0, 1, 1);
}
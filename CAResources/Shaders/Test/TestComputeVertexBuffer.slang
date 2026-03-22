
struct TestComputeBufferParams
{
    RWStructuredBuffer<float> RWVertexBuffer;
    float time;
};

ParameterBlock<TestComputeBufferParams> computeParams;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const float3[] positions = {
        float3(-0.5f, -0.5f, 0.5f),
        float3(0.5f, -0.5f, 0.5f),
        float3(0.5f, 0.5f, 0.5f),
        float3(-0.5f, 0.5f, 0.5f),
    };
    for (int i = 0; i < 4; i++)
    {
        int offset = i * 3;
        computeParams.RWVertexBuffer[offset] = positions[i].x;
        computeParams.RWVertexBuffer[offset + 1] = positions[i].y + 0.2f * sin(computeParams.time + i);
        computeParams.RWVertexBuffer[offset + 2] = positions[i].z;
    }
}

static float3 positions[] = {
    float3(-0.5f, -0.5f, 0.5f),
    float3(0.5f, -0.5f, 0.5f),
    float3(0.0f, 0.5f, 0.5f),
};

struct VertexInput
{
    uint vertexID : SV_VertexID;
};

struct VertexToFragment
{
    float4 position : SV_POSITION;
};

[shader("vertex")]
VertexToFragment vert(in VertexInput input)
{
    VertexToFragment result = (VertexToFragment)0;
    result.position = float4(positions[input.vertexID], 1.0f);
    return result;
}

[shader("fragment")]
float4 frag(in VertexToFragment input) : SV_TARGET0
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f); // Output a solid red color
}
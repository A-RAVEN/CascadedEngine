
struct VertexInput
{
    float3 position : POSITION;
    float3 color : COLOR; 
};

struct VertexToFragment
{
    float4 position : SV_POSITION;
    float3 color : TEXCOORD0;
};

[shader("vertex")]
VertexToFragment vert(in VertexInput input)
{
    VertexToFragment result = (VertexToFragment)0;
    result.position = float4(input.position, 1.0f);
    result.color = input.color;
    return result;
}

[shader("fragment")]
float4 frag(in VertexToFragment input) : SV_TARGET0
{
    return float4(input.color, 1.0f);
}
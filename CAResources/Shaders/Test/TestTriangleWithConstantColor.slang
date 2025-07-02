
struct VertexInput
{
    float3 position : POSITION;
};

struct VertexToFragment
{
    float4 position : SV_POSITION;
};

struct ConstantColor
{
    float3 color;
};

ParameterBlock<ConstantColor> constantColorBlock;

[shader("vertex")]
VertexToFragment vert(in VertexInput input)
{
    VertexToFragment result = (VertexToFragment)0;
    result.position = float4(input.position, 1.0f);
    return result;
}

[shader("fragment")]
float4 frag(in VertexToFragment input) : SV_TARGET0
{
    return float4(constantColorBlock.color, 1.0f);
}
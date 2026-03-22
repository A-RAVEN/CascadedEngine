
struct VertexInput
{
    float3 position : POSITION;
    uint vertexID : SV_VertexID;
};

struct VertexToFragment
{
    float3 color : TEXCOORD0;
    float4 position : SV_POSITION;
};

struct ColorSubStruct
{
    float3 color;
};

struct ConstantColor
{
    ColorSubStruct colorStruct[3];
    //float3 color;
};

ParameterBlock<ConstantColor> constantColorBlock;

[shader("vertex")]
VertexToFragment vert(in VertexInput input)
{
    VertexToFragment result = (VertexToFragment)0;
    result.position = float4(input.position, 1.0f);
    result.color = constantColorBlock.colorStruct[input.vertexID].color;
    return result;
}

[shader("fragment")]
float4 frag(in VertexToFragment input) : SV_TARGET0
{
    return float4(input.color, 1.0f);
}
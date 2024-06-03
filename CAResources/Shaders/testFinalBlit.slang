

// struct Textures
// {
//     Texture2D<float4> SourceTexture;
//     SamplerState SourceSampler;

//     float4 SampleMip0(in float2 uv)
//     {
//         return SourceTexture.SampleLevel(SourceSampler, uv, 0).xyzw;
//     }
// }
// ParameterBlock<Textures> textures;

float4 TestColor;

struct VertexToFragment
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
};

struct VertexInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0; 
    float3 color : COLOR; 
};

[shader("vertex")]
VertexToFragment vert(in VertexInput input)
{
    VertexToFragment result = (VertexToFragment)0;
    result.position = float4(input.position, 0.5, 1);
    result.position.y = -result.position.y;
    result.color = input.color;
    result.uv = input.uv;
    return result;
}


[shader("fragment")]
float4 frag(in VertexToFragment input) : SV_TARGET0
{
    return float4(input.color.xyz, 1.0);// textures.SampleMip0(input.uv).xyzw;
}
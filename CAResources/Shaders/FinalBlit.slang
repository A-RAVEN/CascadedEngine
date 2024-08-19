Texture2D<float4> SourceTexture;
SamplerState SourceSampler;

struct VertexInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0; 
    float3 color : COLOR; 
};

struct VertexToFragment
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexToFragment vert(in VertexInput input)
{
    VertexToFragment result = (VertexToFragment)0;
    result.position = float4(input.position, 0.5, 1);
    result.color = input.color;
    result.uv = input.uv;
    result.uv.y = 1.0 - result.uv.y;
    return result;
}

[shader("fragment")]
float4 frag(in VertexToFragment input) : SV_TARGET0
{
    return SourceTexture.SampleLevel(SourceSampler, input.uv, 0).xyzw;
}
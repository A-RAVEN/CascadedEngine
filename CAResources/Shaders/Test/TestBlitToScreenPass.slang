
struct VertexInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD; 
};

struct VertexToFragment
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0; 
};

struct SamplingTextureData
{
    Texture2D<float4> testTexture;
    SamplerState testSampler;
    float4 Sample(in float2 uv)
    {
        float4 result = testTexture.Sample(testSampler, uv);
        return result;
    }
};

ParameterBlock<SamplingTextureData> textureData;

[shader("vertex")]
VertexToFragment vert(in VertexInput input)
{
    VertexToFragment result = (VertexToFragment)0;
    result.position = float4(input.position, 1.0f);
    result.uv = input.uv;
    return result;
}

[shader("fragment")]
float4 frag(in VertexToFragment input) : SV_TARGET0
{
    return float4(textureData.Sample(input.uv).rgb * float3(1.0f, 0.1f, 0.1f), 1.0f);
}
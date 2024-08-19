

struct Textures
{
    Texture2D<float4> SourceTexture;
    SamplerState SourceSampler;

    float4 SampleMip0(in float2 uv)
    {
        return SourceTexture.SampleLevel(SourceSampler, uv, 0).xyzw;
    }

    float4 SampleExternal(in float2 uv, in Texture2D<float4> texture)
    {
        return texture.SampleLevel(SourceSampler, uv, 0).xyzw;
    }
}
ParameterBlock<Textures> textures;

uniform Texture2D<float4> SourceTexture1;
uniform float4 TestColor;

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
    float change = input.uv.x < 0.5 ? 0.0 : 1.0;
    return lerp(textures.SampleMip0(input.uv).xyzw, textures.SampleExternal(input.uv, SourceTexture1).xyzw, change);
}
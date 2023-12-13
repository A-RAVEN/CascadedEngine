[[vk::binding(0, 0)]]
cbuffer testBuffer
{
    float2 IMGUIScale;
};

[[vk::binding(1, 0)]]
Texture2D<float4> FontTexture;

[[vk::binding(2, 0)]]
SamplerState FontSampler;

struct VSInput
{
    [[vk::location(0)]] float2 Pos : POSITION0;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
    [[vk::location(2)]] float4 Color : COLOR0;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float2 UV : TEXCOORD0;
    [[vk::location(1)]] float4 Color : COLOR0;
};

VSOutput vert(VSInput input)
{
	VSOutput output = (VSOutput)0;
	output.UV = input.UV;
	output.Color = input.Color;
	output.Pos = float4(input.Pos.xy * IMGUIScale - 1.0, 0.0, 1.0);
	return output;
}

float4 frag(VSOutput input) : SV_TARGET
{
	return input.Color * FontTexture.Sample(FontSampler, input.UV);
}
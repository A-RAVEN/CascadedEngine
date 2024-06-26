Texture2D<float4> IMGUITexture;
struct ImguiContext
{
    float4 IMGUIScale_Pos;
    SamplerState FontSampler;

    float4 SampleFont(in float2 uv)
    {
        return IMGUITexture.SampleLevel(FontSampler, uv, 0).xyzw;
    }

    float2 TransformIMGUIVertex(in float2 inVert)
    {
        return (inVert - IMGUIScale_Pos.zw) * IMGUIScale_Pos.xy - 1.0f;
    }
};
ParameterBlock<ImguiContext> imguiCommon;
struct VSInput
{
    float2 Pos : POSITION0;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};

[shader("vertex")]
VSOutput vert(VSInput input)
{
	VSOutput output = (VSOutput)0;
	output.UV = input.UV;
	output.Color = input.Color;
	output.Pos = float4(imguiCommon.TransformIMGUIVertex(input.Pos), 0.0, 1.0);
	return output;
}

[shader("fragment")]
float4 frag(VSOutput input) : SV_TARGET
{
	return input.Color * imguiCommon.SampleFont(input.UV);
}
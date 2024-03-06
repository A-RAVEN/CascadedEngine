struct ImguiContext
{
    float4 IMGUIScale_Pos;
    Texture2D<float4> FontTexture;
    SamplerState FontSampler;

    float4 SampleFont(in float2 uv)
    {
        return FontTexture.SampleLevel(FontSampler, uv, 0).xyzw;
    }

    float2 TransformIMGUIVertex(in float2 inVert)
    {
        return (inVert - IMGUIScale_Pos.zw) * IMGUIScale_Pos.xy - 1.0f;
    }
};
ParameterBlock<ImguiContext> imgui;

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

VSOutput vert(VSInput input)
{
	VSOutput output = (VSOutput)0;
	output.UV = input.UV;
	output.Color = input.Color;
	output.Pos = float4(imgui.TransformIMGUIVertex(input.Pos), 0.0, 1.0);
	return output;
}

float4 frag(VSOutput input) : SV_TARGET
{
	return input.Color * imgui.SampleFont(input.UV);
}
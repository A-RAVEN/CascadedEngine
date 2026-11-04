
// C# 纹理采样测试着色器（csharp-render-texture-sampling 任务 1.2）。
//
// 与原生 `TestTriangleWithTextureSampling` 的关键区别：那个着色器的片元逐字
// `return float4(input.uv, textureData.Sample(input.uv).a, 1.0f);` —— 输出的是 UV 插值，
// 证明不了"纹理字节进了像素"。这里输出**采样色本身**（premultiplied 风格与原生一致，
// 测试纹理 alpha = 255 时不影响 rgb），像素判据才站得住。
//
// 顶点输入两条属性：POSITION `float3`、TEXCOORD `float2`（对应适配层的
// `SetPositionTextureStream`，TEXCOORD 的顶点格式是 8 字节的 `eR32G32_SFloat`）。

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

struct TextureSampleBlock
{
    Texture2D<float4> testTexture;
    SamplerState testSampler;
};

ParameterBlock<TextureSampleBlock> textureBlock;

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
    float4 sampled = textureBlock.testTexture.Sample(textureBlock.testSampler, input.uv);
    sampled.rgb *= sampled.a; // Premultiplied alpha
    return sampled;
}

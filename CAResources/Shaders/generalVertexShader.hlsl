
[[vk::binding(0, 0)]]
cbuffer PerViewConstants
{
    float4x4 ViewProjectionMatrix;
};

[[vk::binding(1, 0)]]
StructuredBuffer<float4x4> InstanceMatBuffer;

[[vk::binding(0, 1)]]
Texture2D<float4> SourceTexture;

[[vk::binding(1, 1)]]
SamplerState SourceSampler;

struct VertexToFragment
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 normal : TEXCOORD0;
};

struct VertexInput
{
    [[vk::location(0)]] uint instanceID : INSTANCEID;
    [[vk::location(1)]] float3 position : POSITION;
    [[vk::location(2)]] float2 uv : TEXCOORD0; 
    [[vk::location(3)]] float3 normal : NORMAL; 
    [[vk::location(4)]] float3 tangent : TANGENT; 
    [[vk::location(5)]] float3 itangent : BITANGENT; 
};

VertexToFragment vert(in VertexInput input)
{
    VertexToFragment result = (VertexToFragment)0;
    float4x4 worldMat = InstanceMatBuffer[input.instanceID];
    result.position = mul(ViewProjectionMatrix, mul(worldMat, float4(input.position, 1)));
    result.color = float3(1, 1, 1);
    result.uv = input.uv;
    result.normal = mul((float3x3)worldMat, input.normal);
    return result;
}

[[vk::location(0)]] float4 frag(in VertexToFragment input) : SV_TARGET0
{
    float4 diffuse = SourceTexture.SampleLevel(SourceSampler, input.uv, 0).xyzw;
    return float4(input.color * diffuse.xyz * (saturate(dot(normalize(input.normal), 1.0)) * 0.5 + 0.5), 1.0);
}
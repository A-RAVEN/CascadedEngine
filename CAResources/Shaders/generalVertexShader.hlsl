
[[vk::binding(0, 0)]]
cbuffer PerViewConstants
{
    float4x4 ViewProjectionMatrix;
};

[[vk::binding(1, 0)]]
StructuredBuffer<float4x4> InstanceMatBuffer;

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
    result.position = mul(ViewProjectionMatrix, mul(InstanceMatBuffer[input.instanceID], float4(input.position, 1)));
    result.color = float3(1, 1, 1);
    result.uv = input.uv;
    result.normal = input.normal;
    return result;
}

[[vk::location(0)]] float4 frag(in VertexToFragment input) : SV_TARGET0
{
    return float4(input.color * (saturate(dot(normalize(input.normal), 1.0)) * 0.5 + 0.5), 1.0);
}
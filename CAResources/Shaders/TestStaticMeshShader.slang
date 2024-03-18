#include "InstancedStaticMesh.hslang"
ParameterBlock<MeshData> meshData;

struct Textures
{
    Texture2D<float4> SourceTexture;
    SamplerState SourceSampler;

    float4 SampleMip0(in float2 uv)
    {
        return SourceTexture.SampleLevel(SourceSampler, uv, 0).xyzw;
    }
}
ParameterBlock<Textures> textures;

struct VertexToFragment
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 normal : TEXCOORD0;
};

struct VertexInput
{
    uint instanceID : INSTANCEID;
    float3 position : POSITION;
    float2 uv : TEXCOORD0; 
    float3 normal : NORMAL; 
    float3 tangent : TANGENT; 
    float3 itangent : BITANGENT; 
};

[shader("vertex")]
VertexToFragment vert(in VertexInput input)
{
    VertexToFragment result = (VertexToFragment)0;
    result.position = meshData.TransformInstanceToClip(input.instanceID, input.position);
    result.color = float3(1, 1, 1);
    result.uv = input.uv;
    result.normal = meshData.TransformDirectionToWorld(input.instanceID, input.normal);
    return result;
}

[shader("fragment")]
float4 frag(in VertexToFragment input) : SV_TARGET0
{
    float4 diffuse = textures.SampleMip0(input.uv).xyzw;
    return float4(input.color * diffuse.xyz * (saturate(dot(normalize(input.normal), 1.0)) * 0.5 + 0.5), 1.0);
}
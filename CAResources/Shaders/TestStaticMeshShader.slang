#include "InstancedStaticMesh.hslang"

struct TestStruct2
{
    float AAA;
};

struct TestStruct3
{
    float BBB;
};

struct TestStruct4
{
    float CCC;
    float3 DDD[3];
    //Texture2D<float> textArray[4];
};

struct Textures
{
    Texture2D<float4> SourceTexture;
    SamplerState SourceSampler;

    // Texture2D<float4> TestTextureArray[5];
    // TestStruct4 TestStructArray[5];
    // float4 Test0;
    // TestStruct2 TestStruct;
    // ConstantBuffer<TestStruct3> TestInlineBlock;

    float4 SampleMip0(in float2 uv)
    {
        return SourceTexture.SampleLevel(SourceSampler, uv, 0).xyzw;// * Test0 * TestInlineBlock.BBB * TestStructArray[2].CCC;
    }
}


Texture2D<float4> TestTexture;
ConstantBuffer<TestStruct3> TestConstantBuffer;
ParameterBlock<Textures> textures;
ParameterBlock<MeshData> meshData;
uniform Textures TestConstantStruct;
uniform float4 TestConstant0;
uniform float4 TestConstant1;

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
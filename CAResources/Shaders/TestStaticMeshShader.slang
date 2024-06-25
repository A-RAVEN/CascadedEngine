#include "InstancedStaticMesh.hslang"
#include "MeshMaterial.hslang"
#include "CameraData.hslang"

ParameterBlock<MeshData> meshInstanceTransforms;
ParameterBlock<MeshMaterial> meshMaterialData;
ConstantBuffer<CameraData> cameraData;
ParameterBlock<GlobalDirectLighting> globalLighting;

struct VertexToFragment
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 wsPos : TEXCOORD1;
    float3 normal : TEXCOORD0;
};

struct NormalStruct
{
    float3 normal : NORMAL; 
    float3 tangent : TANGENT; 
    float3 itangent : BITANGENT; 
}

struct VertexInput
{
    uint instanceID : INSTANCEID;
    float3 position : POSITION;
    float2 uv : TEXCOORD0; 
    NormalStruct normalData;
};

[shader("vertex")]
VertexToFragment vert(in VertexInput input)
{
    VertexToFragment result = (VertexToFragment)0;
    result.wsPos = meshInstanceTransforms.TransformPointToWorld(input.instanceID, input.position);
    result.position = cameraData.WorldToClip(result.wsPos);
    result.uv = input.uv;
    result.normal = meshInstanceTransforms.TransformDirectionToWorld(input.instanceID, input.normalData.normal);
    return result;
}

[shader("fragment")]
float4 frag(in VertexToFragment input) : SV_TARGET0
{
    return meshMaterialData.GetColor(input.uv, input.wsPos, input.normal, globalLighting);
}
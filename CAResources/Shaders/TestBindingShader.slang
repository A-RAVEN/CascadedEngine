struct TestDataStruct1
{
    float4x4 testmat;
};

struct TestDataStruct
{
    ConstantBuffer<TestDataStruct1> testSubBlock[2];
    Texture2D<float4> testTexture;
    StructuredBuffer<float4> testBuffer;
};
uniform float4x4 testMatrix1;
TestDataStruct testData[2];


struct VertexInput
{
    float3 position : POSITION;
};

struct VertexToFragment
{
    float4 position : SV_POSITION;
};

[shader("vertex")]
VertexToFragment vert(in VertexInput input)
{
    VertexToFragment result = (VertexToFragment)0;
    result.position = float4(input.position, 1.0f);
    return result;
}

[shader("fragment")]
float4 frag(in VertexToFragment input) : SV_TARGET0
{
    return 1.0f;
}
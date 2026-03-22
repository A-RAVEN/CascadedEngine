
namespace TestData
{
    struct TestData3
    {
        float3 testValue;
    };

    struct TestDataStruct2
    {
        float4 testValue0;
        Texture2D<float4> testTexture;
        Texture2D<float4> testTexture1;
        Texture2D<float4> testTexture2;
        StructuredBuffer<TestData3> testBuffer0;
        RWStructuredBuffer<TestData3> rwtestBuffer0;
        //ConstantBuffer<TestData3> testCBuffer0;
    };

    struct TestDataStruct1
    {
        TestDataStruct2 testSubBlock[2];
    };

    struct TestDataStruct
    {
        TestDataStruct1 testSubBlock[2];
    };

    ParameterBlock<TestDataStruct> testData;
    //ParameterBlock<TestDataStruct> testData1;
}

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
    return TestData::testData.testSubBlock[0].testSubBlock[0].testValue0;
}
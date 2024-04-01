#include "TestShaderProvider.h"

uint64_t TestShaderProvider::GetDataLength(castl::string const& codeType) const
{
    auto found = m_Data.find(castl::to_std(codeType));
    if (found != m_Data.end())
    {
        return found->second.second.size();
    }
    return 0;
}

void const* TestShaderProvider::GetDataPtr(castl::string const& codeType) const
{
    auto found = m_Data.find(castl::to_std(codeType));
    if (found != m_Data.end())
    {
        return found->second.second.data();
    }
    return nullptr;
}

castl::string TestShaderProvider::GetUniqueName() const
{
    return castl::to_ca(m_UniqueName);
}

ShaderSourceInfo TestShaderProvider::GetDataInfo(castl::string const& codeType) const
{
    const static castl::string invalidEntrypoint = "invalidEntryPoint";
    auto found = m_Data.find(castl::to_std(codeType));
    if (found != m_Data.end())
    {
        return ShaderSourceInfo{
            found->second.second.size()
                , found->second.second.data()
                , castl::to_ca(found->second.first)};
    }
    return ShaderSourceInfo{
        0
            , nullptr
            , invalidEntrypoint};
}
void TestShaderProvider::SetUniqueName(std::string const& uniqueName)
{
    m_UniqueName = uniqueName;
}
void TestShaderProvider::SetData(std::string const& codeType
    , std::string const& entryPoint
    , void const* dataPtr
    , uint64_t dataLength)
{
    std::vector<char> data;
    data.resize(dataLength);
    memcpy(data.data(), dataPtr, dataLength);
    auto found = m_Data.find(codeType);
    if (found != m_Data.end())
    {
        found->second = std::make_pair(entryPoint, data);
    }
    else
    {
        m_Data.insert(std::make_pair(codeType, std::make_pair(entryPoint, data)));
    }
}
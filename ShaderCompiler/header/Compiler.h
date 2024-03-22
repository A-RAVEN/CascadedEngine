#pragma once
#include <Common.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAUnorderedMap.h>

namespace ShaderCompilerSlang
{

	enum class EShaderTargetType : uint8_t
	{
		eSpirV,
		eDXIL,
	};

	class ShaderCompileTargetResult
	{
	public:
		EShaderTargetType targetType;
		castl::vector<ShaderProgramData> programs;
		ShaderReflectionData m_ReflectionData;
	};



	struct ShaderElements
	{
		//Name
		castl::string m_Path;

		//Memory Relate To Shader and Subelements
		uint32_t m_MemoryOffset;
		uint32_t m_MemorySize;

		//Bindings
		uint32_t m_BindingIndex;

		//Subelements for 
		castl::vector<uint32_t> m_SubElements;
	};

	struct UniformArrayElement
	{
		castl::string m_Path;
		uint32_t m_MemoryOffset;
		uint32_t m_MemorySize;

		uint32_t m_ElementCount;
		uint32_t m_SubElementData;
	};

	class ShaderBindingSpaceData
	{
		uint32_t m_BindingSpace;
		castl::vector<uint32_t> m_UniformBufferList;
		castl::vector<uint32_t> m_ResourceBufferList;
		castl::vector<uint32_t> m_ResourceImageList;
		castl::vector<uint32_t> m_ResourceSamplerList;
	};

	class ShaderReflectionData
	{
	public:
		castl::vector<ShaderBindingSpaceData> m_BindingData;
		castl::vector<ShaderElements> m_ShaderElements;

		void EnsureUniformBuffer(uint32_t bindingSpace, uint32_t bindingIndex, castl::string const& path, uint32_t memorySize)
		{
			for(auto& m_BindingData )
		}
	};

	class ShaderProgramData
	{
	public:
		ECompileShaderType shaderType;
		castl::vector<uint8_t> data;
		castl::string entryPointName;
	};


	class IShaderCompiler
	{
	public:
		virtual void AddInlcudePath(const char* path) = 0;
		virtual void SetTarget(EShaderTargetType targetType) = 0;
		virtual void SetMacro(const char* macro_name, const char* macro_value) = 0;
		virtual void BeginCompileTask() = 0;
		virtual void EndCompileTask() = 0;
		virtual void AddSourceFile(const char* path) = 0;
		virtual int AddEntryPoint(const char* name, ECompileShaderType shader_type) = 0;
		virtual void EnableDebugInfo() = 0;
		virtual void Compile() = 0;
		virtual bool HasError() const = 0;
		virtual void const* GetOutputData(int entryPointID, uint64_t& dataSize) const = 0;
		virtual castl::vector<ShaderCompileTargetResult> GetResults() const = 0;
	};

	class IShaderCompilerManager
	{
	public:
		virtual IShaderCompiler* AquireShaderCompiler() = 0;
		virtual void ReturnShaderCompiler(IShaderCompiler* compiler) = 0;
		virtual void InitializePoolSize(uint32_t compiler_count) = 0;

		inline castl::shared_ptr<IShaderCompiler> AquireShaderCompilerShared()
		{
			return castl::shared_ptr<IShaderCompiler>(AquireShaderCompiler(), [this](IShaderCompiler* compiler) {ReturnShaderCompiler(compiler); });
		}
	};

}
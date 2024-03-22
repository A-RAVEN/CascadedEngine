#pragma once
#include <CASTL/CASharedPtr.h>
#include <Common.h>
#include <CASTL/CAUnorderedMap.h>

namespace ShaderCompilerSlang
{

	enum class EShaderTargetType : uint8_t
	{
		eSpirV,
		eDXIL,
	};

	class ShaderProgramData
	{
	public:
		ECompileShaderType shaderType;
		castl::vector<uint8_t> data;
		castl::string entryPointName;
		
		//TODO: Reflection Data
	};

	class ShaderCompileTargetResult
	{
	public:
		EShaderTargetType targetType;
		castl::vector<ShaderProgramData> programs;
	};

	struct ShaderStructElementData
	{
		uint32_t m_Offset;
		uint32_t m_Size;
		int32_t m_StructDataIndex;
	};

	//Struct Data Include Scalar, Vector, Matrix, Struct
	class ShaderStructDataReflection
	{
		uint32_t m_BufferSize;
		castl::unordered_map<castl::string, uint32_t> m_NameToData;
		castl::vector<ShaderStructElementData> m_Data;
	};

	//Uniform Buffer Data, Pointing to a struct
	class ShaderUniformBufferData
	{
		uint32_t m_BindingIndex;
		uint32_t m_BindingSpace;
		uint32_t m_BufferSize;
		int32_t m_StructDataIndex;
	};

	class ShaderResourceBufferData
	{
		uint32_t m_BindingIndex;
		uint32_t m_BindingSpace;
	};

	class ShaderReflectionData
	{
	public:
		castl::vector<ShaderUniformBufferData> m_UniformBufferList;
		castl::vector<ShaderStructDataReflection> m_StructDataList;
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
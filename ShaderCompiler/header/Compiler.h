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



	//某个数值：scalar，vector，matrix
	struct UniformElement
	{
		castl::string m_Name;
		uint32_t m_MemoryOffset;
		uint32_t m_ElementMemorySize;

		//
		uint32_t m_Stride;
		uint32_t m_ElementCount;

		bool isArray() const
		{
			m_ElementCount > 1;
		}
		void Init(castl::string const& name, uint32_t memoryOffset, uint32_t memorySize, uint32_t stride, uint32_t elementCount)
		{
			m_Name = name;
			m_MemoryOffset = memoryOffset;
			m_ElementMemorySize = memorySize;
			m_Stride = stride;
			m_ElementCount = elementCount;
		}
	};

	struct UniformGroup
	{
		castl::string m_Name;
		uint32_t m_MemoryOffset;
		uint32_t m_MemorySize;
		uint32_t m_Stride;
		uint32_t m_ElementCount;
		castl::vector<UniformElement> m_Elements;
		castl::vector<uint32_t> m_SubGroups;
		bool isArray() const
		{
			m_ElementCount > 1;
		}

		void Init(castl::string const& name, uint32_t memoryOffset, uint32_t memorySize, uint32_t stride, uint32_t elementCount)
		{
			m_Name = name;
			m_MemoryOffset = memoryOffset;
			m_MemorySize = memorySize;
			m_Stride = stride;
			m_ElementCount = elementCount;
		};
	};

	struct UniformBufferData
	{
		uint32_t m_BindingIndex;
		//UniformGroup
		castl::vector<UniformGroup> m_Groups = { {} };

		void Init(uint32_t bindingIndex)
		{
			m_BindingIndex = bindingIndex;
			m_Groups.clear();
		}

		void AddSubGroupToGroup(uint32_t groupID, uint32_t subGroupID)
		{
			m_Groups[groupID].m_SubGroups.push_back(subGroupID);
		}

		void AddElementToGroup(uint32_t groupID, UniformElement const& element)
		{
			m_Groups[groupID].m_Elements.push_back(element);
		};

		int32_t NewGroup()
		{
			m_Groups.push_back(UniformGroup{});
			return m_Groups.size() - 1;
		}

		UniformGroup& GetGroup(uint32_t groupID)
		{
			return m_Groups[groupID];
		}
	};

	class ShaderBindingSpaceData
	{
	public:

		UniformBufferData& GetUniformBuffer(uint32_t bindingIndex)
		{
			UniformBufferData* pResult = nullptr;
			for (int i = 0; i < m_UniformBuffers.size(); ++i)
			{
				if (m_UniformBuffers[i].m_BindingIndex == bindingIndex)
				{
					pResult = &m_UniformBuffers[i];
				}
			}
			if (pResult == nullptr)
			{
				m_UniformBuffers.push_back(UniformBufferData{});
				pResult = &m_UniformBuffers.back();
				pResult->Init(bindingIndex);
			}
			return *pResult;
		}

		int32_t InitUniformGroup(uint32_t bindingIndex, int32_t parentGroupID
			, castl::string const& name, uint32_t memoryOffset, uint32_t memorySize, uint32_t memoryStride, uint32_t elementCount = 1)
		{
			if(memorySize == 0 || memoryStride == 0 || elementCount == 0)
			{
				return parentGroupID;
			}
			UniformBufferData& buffer = GetUniformBuffer(bindingIndex);
			int32_t newGroupID = buffer.NewGroup();
			buffer.GetGroup(newGroupID).Init(name, memoryOffset, memorySize, memoryStride, elementCount);
			if (parentGroupID >= 0)
			{
				buffer.AddSubGroupToGroup(parentGroupID, newGroupID);
			}
			return newGroupID;
		}

		void AddElementToGroup(uint32_t bindingIndex, int32_t groupID, UniformElement const& element)
		{
			UniformBufferData& buffer = GetUniformBuffer(bindingIndex);
			buffer.AddElementToGroup(groupID, element);
		}

		uint32_t m_BindingSpace;
		castl::vector<UniformBufferData> m_UniformBuffers;
	};

	class ShaderReflectionData
	{
	public:
		castl::vector<ShaderBindingSpaceData> m_BindingData;

		ShaderBindingSpaceData& EnsureBindingSpace(uint32_t bindingSpace)
		{
			if(m_BindingData.size() < bindingSpace + 1)
			{
				size_t beginID = m_BindingData.size();
				m_BindingData.resize(bindingSpace + 1);
				for(size_t i = beginID; i < m_BindingData.size(); ++i)
				{
					m_BindingData[i].m_BindingSpace = i;
				}
			}
			return m_BindingData[bindingSpace];
		}
	};

	class ShaderProgramData
	{
	public:
		ECompileShaderType shaderType;
		castl::vector<uint8_t> data;
		castl::string entryPointName;
	};

	class ShaderCompileTargetResult
	{
	public:
		EShaderTargetType targetType;
		castl::vector<ShaderProgramData> programs;
		ShaderReflectionData m_ReflectionData;
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
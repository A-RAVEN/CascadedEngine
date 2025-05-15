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

	enum class EShaderResourceAccess : uint8_t
	{
		eUnknown = 0,
		eReadOnly = 1,
		eWriteOnly = 2,
		eReadWrite = 3,
	};

	using EShaderResourceAccessFlags = uenum::EnumFlags<EShaderResourceAccess>;

	enum class EShaderResourceType : uint8_t
	{
		eTexture,
		eRWTexture,
		eSampler,
		eStructuredBuffer,
		eRWStructuredBuffer,
		eCBuffer,
	};

	//某个数值：scalar，vector，matrix，也可能是某个结构体
	struct UniformElement
	{
		cacore::NameHash m_TypeName;
		cacore::NameHash m_Name;
		uint32_t m_MemoryOffset;
		uint32_t m_ElementMemorySize;

		//
		uint32_t m_Stride;
		uint32_t m_ElementCount;

		bool isArray() const
		{
			return m_ElementCount > 1;
		}
		void Init(cacore::NameHash const& typeName, cacore::NameHash const& name, uint32_t memoryOffset, uint32_t memorySize, uint32_t stride, uint32_t elementCount)
		{
			m_TypeName = typeName;
			m_Name = name;
			m_MemoryOffset = memoryOffset;
			m_ElementMemorySize = memorySize;
			m_Stride = stride;
			m_ElementCount = elementCount;
		}
	};

	struct ShaderStructUniforms
	{
		uint32_t m_MemorySize;
		uint32_t m_Stride;
		castl::vector<UniformElement> m_Elements;

		void SetSize(uint32_t size, uint32_t stride)
		{
			m_MemorySize = size;
			m_Stride = stride;
		}

		void EnsureElement(UniformElement const& element)
		{
			for (auto& e : m_Elements)
			{
				if (e.m_Name == element.m_Name)
				{
					return;
				}
			}
			m_Elements.push_back(element);
		}
	};

	

	struct BufferData
	{
		cacore::NameHash m_Name;
		EShaderResourceAccess m_RWType;
		uint32_t m_ElementCount;
	};

	struct ShaderTextureData
	{
		cacore::NameHash m_Name;
		EShaderResourceAccess m_RWType;
		uint32_t m_ElementCount;
	};

	//子结构引用，和StructUniform中的Element不同，这里是另一个binding，只是逻辑上是子结构体
	struct SubStructReference
	{
		cacore::NameHash m_Name;
		cacore::NameHash m_StructTypeName;
		uint32_t m_ElementCount;
	};

	struct ShaderStructData
	{
		cacore::NameHash m_TypeName;
		ShaderStructUniforms m_StructUniforms;
		//Other Non Uniform Resources
		castl::vector<ShaderTextureData> m_Textures;
		castl::vector<cacore::NameHash> m_TextureSamplers;
		castl::vector<BufferData> m_Buffers;
		castl::vector<SubStructReference> m_SubStructReferences;

		void Init(cacore::NameHash const& typeName)
		{
			m_TypeName = typeName;
		}

		void EnsureShaderTexture(ShaderTextureData const& textureData)
		{
			for (auto& texture : m_Textures)
			{
				if (texture.m_Name == textureData.m_Name)
				{
					return;
				}
			}
			m_Textures.push_back(textureData);
		}

		void EnsureShaderBuffer(BufferData const& bufferData)
		{
			for (auto& buffer : m_Buffers)
			{
				if (buffer.m_Name == bufferData.m_Name)
				{
					return;
				}
			}
			m_Buffers.push_back(bufferData);
		}

		void EnsureSubStructReference(SubStructReference const& subStructReference)
		{
			for (auto& subStruct : m_SubStructReferences)
			{
				if (subStruct.m_Name == subStructReference.m_Name)
				{
					return;
				}
			}
			m_SubStructReferences.push_back(subStructReference);
		}

		void EnsureTextureSampler(cacore::NameHash const& samplerName)
		{
			for (auto& sampler : m_TextureSamplers)
			{
				if (sampler == samplerName)
				{
					return;
				}
			}
			m_TextureSamplers.push_back(samplerName);
		}
	};

	//对应到实际的Resource，比如Buffer，Texture，Sampler
	struct ShaderResourceBinding
	{
		uint32_t m_BindingSpace;
		uint32_t m_BindingID;
		cacore::NameHash m_Name;
		cacore::NameHash m_TypeName;
		EShaderResourceAccess m_Access;
		EShaderResourceType m_ResourceType;
		uint32_t m_ElementCount;
	};

	//对应到某个Struct,Struct本身可能数组，所以也有ElementCount
	struct ShaderBindingHierarchy
	{
		cacore::NameHash m_Name;
		cacore::NameHash m_TypeName;
		uint32_t m_ElementCount;
		int32_t m_ParentID;
		int32_t m_SelfUniformBufferID;
		int32_t m_SelfUniformSpaceID;
		//该Struct中的Resource
		castl::vector<ShaderResourceBinding> m_Bindings;
		castl::vector<int32_t> m_SubBindingHierarchies;
	};


	struct ShaderSpaceResourceStats
	{
		struct BindingDesc
		{
			int32_t bindingID;
			uint32_t elementCount;
		};

		struct BufferBindingDesc
		{
			int32_t bindingID;
			uint32_t elementCount;
			uint32_t memoryStride;
		};
		castl::vector<BufferBindingDesc> m_CBufferBindings;
		castl::vector<BindingDesc> m_SamplerBindings;
		castl::vector<BindingDesc> m_TextureBindings;
		castl::vector<BindingDesc> m_RWTextureBindings;
		castl::vector<BindingDesc> m_StorageBufferBindings;
		castl::vector<BindingDesc> m_RWBufferBindings;
		uint32_t m_TotalBindingCount;
		uint32_t m_CBufferCount;
		uint32_t m_SamplerCount;
		uint32_t m_TextureCount;
		uint32_t m_RWTextureCount;
		uint32_t m_StorageBufferCount;
		uint32_t m_RWBufferCount;

	};

	struct ShaderSpaceInfo
	{
		uint32_t m_SpaceID;
		uint32_t m_RootHierarchyID;
		ShaderSpaceResourceStats m_ResourceStats;
	};

	struct ShaderBindingInfo
	{
		castl::vector<ShaderBindingHierarchy> m_BindingDataHierarchies;
		castl::vector<ShaderSpaceInfo> m_SpaceInfos;
		int32_t m_RootHierarchyID;

		ShaderSpaceInfo& EnsureSpaceInfo(uint32_t spaceID, int32_t hierarchyID)
		{
			assert(hierarchyID >= 0);
			if (m_SpaceInfos.size() <= spaceID)
			{
				m_SpaceInfos.resize(spaceID + 1);
				m_SpaceInfos[spaceID] = {};
				m_SpaceInfos[spaceID].m_SpaceID = spaceID;
				m_SpaceInfos[spaceID].m_RootHierarchyID = hierarchyID;
			}
			return m_SpaceInfos[spaceID];
		}

		//ShaderSpaceInfo& TryInitSpaceInfo(uint32_t spaceID, int32_t hierarchyID)
		//{
		//	if (m_SpaceInfos.size() <= spaceID)
		//	{
		//		m_SpaceInfos.resize(spaceID + 1);
		//		m_SpaceInfos[spaceID] = {};
		//		m_SpaceInfos[spaceID].m_SpaceID = spaceID;
		//		m_SpaceInfos[spaceID].m_RootHierarchyID = hierarchyID;
		//	}
		//	return m_SpaceInfos[spaceID];
		//}

		int32_t NewHierarchy(int32_t parentID
			, cacore::NameHash const& name
			, cacore::NameHash const& typeName
			, uint32_t elementCount)
		{

			ShaderBindingHierarchy newHierarchy{};
			newHierarchy.m_Name = name;
			newHierarchy.m_TypeName = typeName;
			newHierarchy.m_ElementCount = elementCount;
			newHierarchy.m_ParentID = parentID;
			newHierarchy.m_SelfUniformBufferID = -1;
			newHierarchy.m_SelfUniformSpaceID = -1;
			if (parentID >= 0)
			{
				assert(parentID < m_BindingDataHierarchies.size());
			}
			m_BindingDataHierarchies.push_back(newHierarchy);
			int32_t newBindingHierarchyID = m_BindingDataHierarchies.size() - 1;
			if (parentID >= 0)
			{
				m_BindingDataHierarchies[parentID].m_SubBindingHierarchies.push_back(newBindingHierarchyID);
			}
			else
			{
				m_RootHierarchyID = newBindingHierarchyID;
			}
			return newBindingHierarchyID;
		}

		ShaderBindingHierarchy& GetHierarchy(int32_t hierarchyID)
		{
			assert(hierarchyID >= 0 && hierarchyID < m_BindingDataHierarchies.size());
			return m_BindingDataHierarchies[hierarchyID];
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
			return m_ElementCount > 1;
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
		castl::vector<UniformGroup> m_Groups;

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

	struct ShaderResourceGroups
	{
		castl::string m_Name;
		castl::vector<uint32_t> m_SubGroups;
		castl::vector<uint32_t> m_Buffers;
		castl::vector<uint32_t> m_Textures;
		castl::vector<uint32_t> m_Samplers;

		void InitResourceGroup(castl::string name)
		{
			m_Name = name;
		}
	};



	struct TextureData
	{
		uint32_t m_BindingIndex;
		uint32_t m_Count;
		EShaderResourceAccess m_Access;
		castl::string m_Name;
	};

	struct SamplerData
	{
		uint32_t m_BindingIndex;
		uint32_t m_Count;
		castl::string m_Name;
	};

	struct ShaderBufferData
	{
		uint32_t m_BindingIndex;
		uint32_t m_Count;
		EShaderResourceAccess m_Access;
		castl::string m_Name;
	};

	struct ShaderBindingSpaceData
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
			, castl::string const& name
			, uint32_t memoryOffset
			, uint32_t memorySize
			, uint32_t memoryStride
			, uint32_t elementCount = 1)
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

		int32_t InitResourceGroup(castl::string const& name, int32_t parentGroupID)
		{
			m_ResourceGroups.push_back(ShaderResourceGroups{});
			m_ResourceGroups.back().InitResourceGroup(name);
			int32_t newGroupID = m_ResourceGroups.size() - 1;
			if (parentGroupID >= 0)
			{
				m_ResourceGroups[parentGroupID].m_SubGroups.push_back(newGroupID);
			}
			return newGroupID;
		}

		void EnsureDefaultResourceGroup(int32_t& groupID)
		{
			if (groupID == -1)
			{
				InitResourceGroup("__Global", groupID);
				groupID = 0;
			}
		}

		void AddBufferToResourceGroup(int32_t groupID, ShaderBufferData const& bufferData)
		{
			EnsureDefaultResourceGroup(groupID);
			CA_ASSERT(groupID >= 0, "groupID must be valid");
			m_Buffers.push_back(bufferData);
			m_ResourceGroups[groupID].m_Buffers.push_back(m_Buffers.size() - 1);
		}

		void AddTextureToResourceGroup(int32_t groupID, TextureData const& textureData)
		{
			EnsureDefaultResourceGroup(groupID);
			CA_ASSERT(groupID >= 0, "groupID must be valid");
			m_Textures.push_back(textureData);
			m_ResourceGroups[groupID].m_Textures.push_back(m_Textures.size() - 1);
		}

		void AddSamplerToResourceGroup(int32_t groupID, SamplerData const& samplerData)
		{
			EnsureDefaultResourceGroup(groupID);
			CA_ASSERT(groupID >= 0, "groupID must be valid");
			m_Samplers.push_back(samplerData);
			m_ResourceGroups[groupID].m_Samplers.push_back(m_Samplers.size() - 1);
		}

		//Binding Space Of This Data
		uint32_t m_BindingSpace;

		//UniformBuffer
		castl::vector<UniformBufferData> m_UniformBuffers;

		//Buffers
		castl::vector<ShaderBufferData> m_Buffers;
		//Textures
		castl::vector<TextureData> m_Textures;
		//Samplers
		castl::vector<SamplerData> m_Samplers;
		//ResourceGroups
		castl::vector<ShaderResourceGroups> m_ResourceGroups;


		uint32_t GetBindingCount() const
		{
			return m_UniformBuffers.size() + m_Buffers.size() + m_Textures.size() + m_Samplers.size();
		}
	};

	struct ShaderVertexAttributeData
	{
		castl::string m_Name;
		castl::string m_SematicName;
		uint32_t m_SematicIndex;
		uint32_t m_Location;
	};

	struct ShaderReflectionData
	{
	public:
		castl::vector<ShaderBindingSpaceData> m_BindingData;
		castl::vector<ShaderVertexAttributeData> m_VertexAttributes;
		castl::unordered_map<cacore::NameHash, ShaderStructData> m_ShaderStructs;
		ShaderBindingInfo m_BindingInfo;

		ShaderStructData& EnsureStruct(cacore::NameHash structTypeName)
		{
			auto found = m_ShaderStructs.find(structTypeName);
			if (found == m_ShaderStructs.end())
			{
				m_ShaderStructs.insert(castl::make_pair(structTypeName, ShaderStructData{}));
				found = m_ShaderStructs.find(structTypeName);
				found->second.Init(structTypeName);
			}
			return found->second;
		}

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

	struct ShaderProgramData
	{
		ECompileShaderType shaderType;
		castl::vector<uint8_t> data;
		castl::string entryPointName;
	};

	struct ShaderCompileTargetResult
	{
		EShaderTargetType targetType;
		EShaderTypeFlags shaderTypeFlags;
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
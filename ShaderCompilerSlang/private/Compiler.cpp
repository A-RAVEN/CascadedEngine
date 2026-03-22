#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
#define NV_EXTENSIONS
#include <Compiler.h>
#define SLANG_STATIC
#include <slang.h>
#include <slang-com-ptr.h>
#include <CASTL/CAMap.h>
#include <CASTL/CASet.h>
#include <CASTL/CAString.h>
#include <CASTL/CAMutex.h>
#include <DebugUtils.h>
//#include <LibraryExportCommon.h>
#include "TestPrint.h"
#define CA_IMPLEMENT_MODULE 1
#include <CACore/CAModuleImplementation.h>


namespace ShaderCompilerSlang
{
	using namespace slang;
	static SlangStage SHADER_KIND_TABLE[] =
	{
		SLANG_STAGE_VERTEX,
		SLANG_STAGE_HULL,
		SLANG_STAGE_DOMAIN,
		SLANG_STAGE_GEOMETRY,
		SLANG_STAGE_FRAGMENT,
		SLANG_STAGE_COMPUTE,

		//nvidia mesh shader
		SLANG_STAGE_AMPLIFICATION,
		SLANG_STAGE_MESH,

		//nvidia raytracing shader
		SLANG_STAGE_RAY_GENERATION,
		SLANG_STAGE_ANY_HIT,
		SLANG_STAGE_CLOSEST_HIT,
		SLANG_STAGE_MISS,
		SLANG_STAGE_INTERSECTION,
		SLANG_STAGE_CALLABLE,
	};

	class Compiler_Impl : public IShaderCompiler
	{
	public:
		Compiler_Impl()
		{
		}
		~Compiler_Impl()
		{
		}

		void Init()
		{
			slang::createGlobalSession(m_Session.writeRef());
		}

		void Release()
		{
			m_Session.setNull();
		}

		virtual void AddInlcudePath(const char* path) override
		{
			AddSearchPath(path);
		}

		virtual void SetTarget(EShaderTargetType targetType) override
		{
			switch (targetType)
			{
			case EShaderTargetType::eSpirV:
				PushTarget(SLANG_SPIRV, "glsl_450");
				break;
			case EShaderTargetType::eHLSL:
				PushTarget(SLANG_HLSL, "sm_6_3");
				break;
			case EShaderTargetType::eDXIL:
				PushTarget(SLANG_DXIL, "sm_6_3");
				break;
			}
		}

		virtual void SetMacro(const char* macro_name, const char* macro_value) override
		{
			SetMacro_Internal(macro_name, macro_value);
		}


		virtual void BeginCompileTask() override
		{
			ClearCompileTask();
		}

		virtual void EndCompileTask() override
		{
			ClearCompileTask();
		}

		virtual void AddSourceFile(const char* path) override
		{
			AddModule(path);
		}

		virtual int AddEntryPoint(const char* name, ECompileShaderType shader_type) override
		{
			return 0;
		};

		virtual void EnableDebugInfo() override
		{
		}

		virtual void Compile() override
		{
			DoCompile();
		}

		virtual void const* GetOutputData(int entryPointID, uint64_t& dataSize) const override
		{
			return nullptr;
		}

		virtual castl::vector<ShaderCompileTargetResult> GetResults() const override
		{
			return m_CompileResults;
		}

		virtual bool HasError() const override
		{
			return m_ErrorList.size() > 0;
		}
		
	private:
		/// <summary>
		/// Global Session
		/// </summary>
		Slang::ComPtr<IGlobalSession> m_Session = nullptr;

		/// <summary>
		/// Compile Task Related
		/// </summary>
		Slang::ComPtr<ISession> m_CompileSession;

		SessionDesc m_CompileSessionDesc = {};
		castl::vector<TargetDesc> m_TargetDescs;
		castl::vector<castl::string> m_SearchPaths;
		castl::map<castl::string, castl::string> m_Macros;
		castl::vector<castl::string> m_ModuleNames;
		castl::vector<castl::string> m_ErrorList;
		castl::vector<ShaderCompileTargetResult> m_CompileResults;
		castl::vector<CompilerOptionEntry> m_CompilerOptionEntries = { 
			CompilerOptionEntry{ CompilerOptionName::VulkanUseEntryPointName , CompilerOptionValue{ CompilerOptionValueKind::Int, 1 }} ,
			CompilerOptionEntry{ CompilerOptionName::EmitSpirvDirectly , CompilerOptionValue{ CompilerOptionValueKind::Int, 1 }} ,
			CompilerOptionEntry{ CompilerOptionName::DebugInformation , CompilerOptionValue{ CompilerOptionValueKind::Int, SLANG_DEBUG_INFO_LEVEL_STANDARD  }} ,
			CompilerOptionEntry{ CompilerOptionName::Optimization , CompilerOptionValue{ CompilerOptionValueKind::Int, SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_NONE }},
			//CompilerOptionEntry{ CompilerOptionName::Optimization , CompilerOptionValue{ CompilerOptionValueKind::Int, SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_HIGH }},
			CompilerOptionEntry{ CompilerOptionName::MatrixLayoutRow , CompilerOptionValue{ CompilerOptionValueKind::Int, 1 }},
			CompilerOptionEntry{ CompilerOptionName::MatrixLayoutColumn , CompilerOptionValue{ CompilerOptionValueKind::Int, 0 }},
			CompilerOptionEntry{ CompilerOptionName::PreserveParameters , CompilerOptionValue{ CompilerOptionValueKind::Int, 0 }},
		};

		void PushTarget(SlangCompileTarget targetType, const char* profileStr)
		{
			
			TargetDesc targetDesc = {};
			targetDesc.format = targetType;
			targetDesc.profile = m_Session->findProfile(profileStr);
			m_TargetDescs.push_back(targetDesc);
		}

		void AddSearchPath(castl::string_view const& pathStr)
		{
			m_SearchPaths.push_back(castl::string(pathStr));
		}

		void SetMacro_Internal(castl::string_view const& name, castl::string_view const& value)
		{
			m_Macros[castl::string(name)] = value;
		};

		void AddModule(castl::string_view const& moduleName)
		{
			m_ModuleNames.push_back(castl::string(moduleName));
		}

		static char const* GetCategoryName(ParameterCategory category)
		{
			switch (category)
			{
				case SLANG_PARAMETER_CATEGORY_NONE:
					return "SLANG_PARAMETER_CATEGORY_NONE";
				case SLANG_PARAMETER_CATEGORY_MIXED:
					return "SLANG_PARAMETER_CATEGORY_MIXED";
				case SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER:
					return "SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER";
				case SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE:
					return "SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE";
				case SLANG_PARAMETER_CATEGORY_UNORDERED_ACCESS:
					return "SLANG_PARAMETER_CATEGORY_UNORDERED_ACCESS";
				case SLANG_PARAMETER_CATEGORY_VARYING_INPUT:
					return "SLANG_PARAMETER_CATEGORY_VARYING_INPUT";
				case SLANG_PARAMETER_CATEGORY_VARYING_OUTPUT:
					return "SLANG_PARAMETER_CATEGORY_VARYING_OUTPUT";
				case SLANG_PARAMETER_CATEGORY_SAMPLER_STATE:
					return "SLANG_PARAMETER_CATEGORY_SAMPLER_STATE";
				case SLANG_PARAMETER_CATEGORY_UNIFORM:
					return "SLANG_PARAMETER_CATEGORY_UNIFORM";
				case SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT:
					return "SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT";
				case SLANG_PARAMETER_CATEGORY_SPECIALIZATION_CONSTANT:
					return "SLANG_PARAMETER_CATEGORY_SPECIALIZATION_CONSTANT";
				case SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER:
					return "SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER";
				case SLANG_PARAMETER_CATEGORY_REGISTER_SPACE:
					return "SLANG_PARAMETER_CATEGORY_REGISTER_SPACE";
				case SLANG_PARAMETER_CATEGORY_GENERIC:
					return "SLANG_PARAMETER_CATEGORY_GENERIC";
				case SLANG_PARAMETER_CATEGORY_RAY_PAYLOAD:
					return "SLANG_PARAMETER_CATEGORY_RAY_PAYLOAD";
				case SLANG_PARAMETER_CATEGORY_HIT_ATTRIBUTES:
					return "SLANG_PARAMETER_CATEGORY_HIT_ATTRIBUTES";
				case SLANG_PARAMETER_CATEGORY_CALLABLE_PAYLOAD:
					return "SLANG_PARAMETER_CATEGORY_CALLABLE_PAYLOAD";
				case SLANG_PARAMETER_CATEGORY_SHADER_RECORD:
					return "SLANG_PARAMETER_CATEGORY_SHADER_RECORD";
				case SLANG_PARAMETER_CATEGORY_EXISTENTIAL_TYPE_PARAM:
					return "SLANG_PARAMETER_CATEGORY_EXISTENTIAL_TYPE_PARAM";
				case SLANG_PARAMETER_CATEGORY_EXISTENTIAL_OBJECT_PARAM:
					return "SLANG_PARAMETER_CATEGORY_EXISTENTIAL_OBJECT_PARAM";
				case SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE:
					return "SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE";
				case SLANG_PARAMETER_CATEGORY_COUNT:
					return "SLANG_PARAMETER_CATEGORY_COUNT";
			}
		}

		static char const* GetBindingTypeName(slang::BindingType type)
		{
			switch (type)
			{
				case slang::BindingType::Unknown						:
					return "Unknown";
				case slang::BindingType::Sampler						:			
					return "Sampler";
				case slang::BindingType::Texture						:
					return "Texture";
				case slang::BindingType::ConstantBuffer					:
					return "ConstantBuffer";
				case slang::BindingType::ParameterBlock					:
					return "ParameterBlock";
				case slang::BindingType::TypedBuffer					:
					return "TypedBuffer";
				case slang::BindingType::RawBuffer						:
					return "RawBuffer";
				case slang::BindingType::CombinedTextureSampler			:
					return "CombinedTextureSampler";
				case slang::BindingType::InputRenderTarget				:
					return "InputRenderTarget";
				case slang::BindingType::InlineUniformData				:
					return "InlineUniformData";
				case slang::BindingType::RayTracingAccelerationStructure:
					return "RayTracingAccelerationStructure";
				case slang::BindingType::VaryingInput					:
					return "VaryingInput";
				case slang::BindingType::VaryingOutput					:
					return "VaryingOutput";
				case slang::BindingType::ExistentialValue				:
					return "ExistentialValue";
				case slang::BindingType::PushConstant					:
					return "PushConstant";
				case slang::BindingType::MutableFlag					:
					return "MutableFlag";
				case slang::BindingType::MutableTexture					:
					return "MutableTexture";
				case slang::BindingType::MutableTypedBuffer				:
					return "MutableTypedBuffer";
				case slang::BindingType::MutableRawBuffer				:
					return "MutableRawBuffer";
				case slang::BindingType::BaseMask						:
					return "BaseMask";
				case slang::BindingType::ExtMask						:
					return "ExtMask";
			}
		}

		static bool IsSpaceRelatedCategories(ParameterCategory category)
		{
			switch (category)
			{
			case slang::ParameterCategory::ConstantBuffer:
			case slang::ParameterCategory::ShaderResource:
			case slang::ParameterCategory::UnorderedAccess:
			case slang::ParameterCategory::SamplerState:
			case slang::ParameterCategory::DescriptorTableSlot:
				return true;
			default:
				return false;
			}
		}

		uint32_t GetArrayElementCount(slang::TypeLayoutReflection* typeLayout)
		{
			uint32_t elementCount = 1;
			auto kind = typeLayout->getKind();
			if (kind == slang::TypeReflection::Kind::Array)
			{
				elementCount = typeLayout->getElementCount();
			}
			return elementCount;
		}

		static slang::TypeLayoutReflection* GetTypeLayoutNonArray(slang::VariableLayoutReflection* variable)
		{
			auto typeLayout = variable->getTypeLayout();
			auto kind = typeLayout->getKind();
			if (kind == slang::TypeReflection::Kind::Array)
			{
				typeLayout = typeLayout->getElementTypeLayout();
			}
			return typeLayout;
		}

		static castl::vector<slang::ParameterCategory> UnwrapCategories(slang::VariableLayoutReflection* variable)
		{
			castl::vector<slang::ParameterCategory> result;
			auto variableCategory = variable->getCategory();
			if (variableCategory == ParameterCategory::Mixed)
			{
				unsigned categoryCount = variable->getCategoryCount();
				result.reserve(categoryCount);
				for (unsigned cc = 0; cc < categoryCount; cc++)
				{
					result.push_back(variable->getCategoryByIndex(cc));
				}
			}
			else
			{
				result.push_back(variableCategory);
			}
			return result;
		}

		struct SpaceAndBinding
		{
			int offset = 0; // the actual offset
			int space = 0; // the associated space
			int elementCount = 1;
			cacore::NameHash name;
		};

		struct AccessPathNode
		{
			slang::VariableLayoutReflection* varLayout;
			AccessPathNode* outer;
		};

		struct AccessPath
		{
			AccessPathNode* leaf = nullptr;
			AccessPathNode* deepestConstantBuffer = nullptr;
			AccessPathNode* deepestParameterBlock = nullptr;

			AccessPathNode NewNode(slang::VariableLayoutReflection* varLayout)
			{
				AccessPathNode node;
				node.varLayout = varLayout;
				node.outer = leaf;
				return node;
			}

			void SetLeaf(AccessPathNode& node)
			{
				leaf = &node;

				if (leaf->outer != nullptr)
				{
					auto parentTypeLayout = GetTypeLayoutNonArray(leaf->outer->varLayout);
					auto kind = parentTypeLayout->getKind();
					switch (kind)
					{
					case slang::TypeReflection::Kind::ConstantBuffer:
					case slang::TypeReflection::Kind::ParameterBlock:
					{
						auto containerVarLayout = parentTypeLayout->getContainerVarLayout();
						auto elementVarLayout = parentTypeLayout->getElementVarLayout();
						CA_ASSERT_BREAK(elementVarLayout == leaf->varLayout, "Current Node Should Be Element Of Parent CBuffer Or Parameter Buffer");
						deepestConstantBuffer = &node;
						if (containerVarLayout->getTypeLayout()->getSize(ParameterCategory::SubElementRegisterSpace) != 0)
						{
							deepestParameterBlock = &node;
						}
						break;
					}
					}
				}
			}

			bool GetLastBufferBinding(SpaceAndBinding& result) const
			{
				result = {};
				if (leaf != nullptr)
				{
					auto kind = leaf->varLayout->getTypeLayout()->getKind();
					switch(kind)
					{
					default:
						CA_LOG_ERR_BREAK("Unexpected Type Kind For Buffer");
						return false;
					case slang::TypeReflection::Kind::ConstantBuffer:
					case slang::TypeReflection::Kind::ParameterBlock:
					case slang::TypeReflection::Kind::TextureBuffer:
					case slang::TypeReflection::Kind::ShaderStorageBuffer:
						break;
					}
					auto varLayout = leaf->varLayout;
					auto typeLayout = GetTypeLayoutNonArray(varLayout);
					auto containerLayout = typeLayout->getContainerVarLayout();
					auto containerCategories = UnwrapCategories(containerLayout);
					CA_ASSERT_BREAK(containerCategories.size() <= 2, "CBuffer Or Parameter Block Should Have Atmost Two Categories?");
					uint32_t spaceRelatedCategoryCount = 0;
					for (auto cbufferCategory : containerCategories)
					{
						if (IsSpaceRelatedCategories(cbufferCategory))
						{
							spaceRelatedCategoryCount++;
						}
					}
					CA_ASSERT_BREAK(spaceRelatedCategoryCount <= 1, "CBuffer Or Parameter Block Should Have Atmost One Space Related Category");
					for (auto cbufferCategory : containerCategories)
					{
						if (IsSpaceRelatedCategories(cbufferCategory))
						{
							for (auto itrNode = leaf; itrNode != deepestParameterBlock; itrNode = itrNode->outer)
							{
								auto varLayout = itrNode->varLayout;
								auto typeLayout = varLayout->getTypeLayout();
								auto category = varLayout->getCategory();
								auto kind = typeLayout->getKind();
								if (category == ParameterCategory::SubElementRegisterSpace)
								{
									result.space += varLayout->getOffset(ParameterCategory::SubElementRegisterSpace);
								}
								else
								{
									result.space += varLayout->getBindingSpace(cbufferCategory);
									result.offset += varLayout->getOffset(cbufferCategory);
								}
								if (kind == slang::TypeReflection::Kind::Array)
								{
									result.elementCount *= typeLayout->getElementCount();
								}
							}
							for (auto node = deepestParameterBlock; node != nullptr; node = node->outer)
							{
								result.space += node->varLayout->getOffset((SlangParameterCategory)slang::ParameterCategory::SubElementRegisterSpace);
							}
							for (auto itrNode = leaf; itrNode != nullptr; itrNode = itrNode->outer)
							{
								auto varLayout = itrNode->varLayout;
								if ((!result.name.Valid()) && (varLayout->getName() != nullptr))
								{
									result.name = varLayout->getName();
									break;
								}
							}
							return true;
						}
					}
				}
				return false;
			}

			SpaceAndBinding GetLeafSpaceAndBinding(ParameterCategory searchingCategory = ParameterCategory::None) const
			{
				SpaceAndBinding result = {};
				auto leafCategory = leaf->varLayout->getCategory();
				if (leafCategory == ParameterCategory::Mixed)
				{
					if (searchingCategory == ParameterCategory::None)
					{
						CA_LOG_ERR_BREAK("Leaf category is mixed, searchingCategory Should Be Specified");
						return result;
					}
					auto categories = UnwrapCategories(leaf->varLayout);
					for (auto category : categories)
					{
						if (category == searchingCategory)
						{
							leafCategory = category;
							break;
						}
					}
				}
				if (leafCategory == ParameterCategory::Mixed)
				{
					CA_LOG_ERR_BREAK("searchCategory Not Found In Leaf");
					return result;
				}
				if (searchingCategory != ParameterCategory::None && searchingCategory != leafCategory)
				{
					CA_LOG_ERR_BREAK("searchCategory Not Matched\n");
					return result;
				}
				if (IsSpaceRelatedCategories(leafCategory))
				{
					for (auto itrNode = leaf; itrNode != deepestParameterBlock; itrNode = itrNode->outer)
					{
						auto varLayout = itrNode->varLayout;
						auto typeLayout = varLayout->getTypeLayout();
						auto category = varLayout->getCategory();
						auto kind = typeLayout->getKind();
						//if (category == slang::ParameterCategory::Mixed)
						//{
						//	auto categories = UnwrapCategories(varLayout);
						//}
						if (category == ParameterCategory::SubElementRegisterSpace)
						{
							result.space += varLayout->getOffset(ParameterCategory::SubElementRegisterSpace);
						}
						else
						{
							result.space += varLayout->getBindingSpace(leafCategory);
							result.offset += varLayout->getOffset(leafCategory);
						}
						if (kind == slang::TypeReflection::Kind::Array)
						{
							result.elementCount *= typeLayout->getElementCount();
						}
					}
					for (auto node = deepestParameterBlock; node != nullptr; node = node->outer)
					{
						result.space += node->varLayout->getOffset((SlangParameterCategory)slang::ParameterCategory::SubElementRegisterSpace);
					}
					for (auto itrNode = leaf; itrNode != nullptr; itrNode = itrNode->outer)
					{
						auto varLayout = itrNode->varLayout;
						if ((!result.name.Valid()) && (varLayout->getName() != nullptr))
						{
							result.name = varLayout->getName();
							break;
						}
					}
				}
				else if (leafCategory != ParameterCategory::Uniform)
				{
					for (auto itrNode = leaf; itrNode != nullptr; itrNode = itrNode->outer)
					{
						result.offset += itrNode->varLayout->getOffset(leafCategory);
					}
				}
				return result;
			}
		};

		struct SpaceAndBindingOffset
		{
			uint32_t space = 0;
			uint32_t binding = 0;

			SpaceAndBindingOffset OffsetSpace(uint32_t spaceOffset)
			{
				SpaceAndBindingOffset newOffset = *this;
				newOffset.space += spaceOffset;
				newOffset.binding = 0;
				return newOffset;
			}

			SpaceAndBindingOffset OffsetBinding(uint32_t bindingOffset)
			{
				SpaceAndBindingOffset newOffset = *this;
				newOffset.binding += bindingOffset;
				return newOffset;
			}

			SpaceAndBindingOffset CumulateOffset(uint32_t spaceOffset, uint32_t bindingOffset) const
			{
				SpaceAndBindingOffset newOffset = *this;
				newOffset.space += spaceOffset;
				newOffset.binding += bindingOffset;
				return newOffset;
			}

			SpaceAndBindingOffset CumulateOffset(slang::VariableLayoutReflection* variable, SlangParameterCategory category) const
			{
				uint32_t bindingSpaceOffset = variable->getBindingSpace(category);
				uint32_t bindingIDOffset = variable->getOffset(category);
				SpaceAndBindingOffset newOffset = *this;
				if (category == SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE)
				{
					newOffset.space += bindingIDOffset;
				}
				else
				{
					newOffset.space += bindingSpaceOffset;
					newOffset.binding += bindingIDOffset;
				}
				return newOffset;
			}
		};

		struct BindingData
		{
			uint32_t bindingSpace = 0;
			uint32_t bindingIndex = 0;
			uint32_t memoryByteOffset = 0;
			castl::string elementName = "";
			castl::string path = "";
			//
			int uniformGroupID = -1;
			int resourceGroupID = -1;

			BindingData OffsetSpace(uint32_t spaceOffset) const
			{
				BindingData newBinding = *this;
				newBinding.bindingSpace += spaceOffset;
				newBinding.bindingIndex = 0;
				newBinding.memoryByteOffset = 0;
				newBinding.uniformGroupID = -1;
				newBinding.resourceGroupID = -1;
				return newBinding;
			}
			BindingData OffsetBindingIndex(uint32_t indexOffset) const
			{
				BindingData newBinding = *this;
				newBinding.bindingIndex += indexOffset;
				return newBinding;
			}
			BindingData OffsetMemory(uint32_t bytesOffset) const
			{
				BindingData newBinding = *this;
				newBinding.memoryByteOffset += bytesOffset;
				return newBinding;
			}
			void OffsetName(const char* name)
			{
				if (name == nullptr)
					return;
				elementName = name;
				path += "." + elementName;
			}
		};

		

		BindingData OffsetBindingDataBySingleCategory(BindingData const& originalBindingData, slang::VariableLayoutReflection* variable, ParameterCategory variableCategory)
		{
			BindingData newBinding;
			if (variableCategory == ParameterCategory::SubElementRegisterSpace)
			{
				uint32_t bindingSpaceOffset = variable->getBindingSpace(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE) + variable->getOffset(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE);
				newBinding = originalBindingData.OffsetSpace(bindingSpaceOffset);
			}
			else if (variableCategory == ParameterCategory::Uniform)
			{
				newBinding = originalBindingData.OffsetMemory(variable->getOffset((SlangParameterCategory)variableCategory));
			}
			else
			{
				newBinding = originalBindingData.OffsetBindingIndex(variable->getOffset((SlangParameterCategory)variableCategory));
			}
			newBinding.OffsetName(variable->getName());
			return newBinding;
		}


		bool MayContainsUniform(ParameterCategory parentCategory)
		{
			return (parentCategory == ParameterCategory::Uniform
				|| parentCategory == ParameterCategory::DescriptorTableSlot
				|| parentCategory == ParameterCategory::ConstantBuffer
				|| parentCategory == ParameterCategory::PushConstantBuffer);
		}


		EShaderResourceAccess TranslateSlangResourceAccess(SlangResourceAccess resourceAccess)
		{
			switch (resourceAccess)
			{
				case SLANG_RESOURCE_ACCESS_READ:
					return EShaderResourceAccess::eReadOnly;
				case SLANG_RESOURCE_ACCESS_WRITE:
					return EShaderResourceAccess::eWriteOnly;
				case SLANG_RESOURCE_ACCESS_READ_WRITE:
					return EShaderResourceAccess::eReadWrite;
			}
			return EShaderResourceAccess::eUnknown;
		}

		

		bool NotUniformCategory(ParameterCategory category)
		{
			switch (category)
			{
			case slang::ParameterCategory::Uniform:
				return false;
			default:
				return true;
			}
		}

		bool CategoryCheck(ParameterCategory fieldCategory, ParameterCategory parentCategory)
		{
			switch (parentCategory)
			{
			case slang::ParameterCategory::Mixed:
				return false;
			case ParameterCategory::SubElementRegisterSpace:
				return true;
			default:
				return fieldCategory == parentCategory;
			}
		}

		static cacore::NameHash const& RootName()
		{
			return CANAME("__Root");
		}

		static cacore::NameHash const& RootTypeName()
		{
			return CANAME("__RootType");
		}

		uint32_t  CollectResourceUsage(SlangParameterCategory category
			, SpaceAndBinding const& bindings
			, castl::vector<slang::IMetadata*> const& metaDatas)
		{
			uint32_t usage = 0;
			for (uint32_t metaID = 0; metaID < metaDatas.size(); ++metaID)
			{
				auto metaData = metaDatas[metaID];
				bool used;
				metaData->isParameterLocationUsed(
					category
					, bindings.space, bindings.offset, used);
				used = true;
				if (used)
				{
					usage |= (1 << metaID);
				}
			}
			return usage;
		}

		castl::string LogUsage(uint32_t usageBits)
		{
			castl::string usages = "Usages:";
			for (uint32_t id = 0; id < 32; ++id)
			{
				if (usageBits & (1 << id))
				{
					usages += castl::to_string(id) + "; ";
				}
			}
			return usages;
		}

		void ReflectConstantBufferBindings(ShaderBindingInfo& bindingInfo
			, VariableLayoutReflection* variable
			, AccessPath accessPath
			, int32_t parentHierarchyID
			, castl::vector<slang::IMetadata*> const& metaDatas)
		{
			AccessPathNode newNode = accessPath.NewNode(variable);
			cacore::NameHash name = variable->getName();
			accessPath.SetLeaf(newNode);
			slang::TypeLayoutReflection* typeLayout = GetTypeLayoutNonArray(variable);
			slang::TypeReflection::Kind kind = typeLayout->getKind();

			ParameterCategory variableCategory = variable->getCategory();
			uint32_t elementCount = GetArrayElementCount(variable->getTypeLayout());
			if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock)
			{
				auto elementVariable = typeLayout->getElementVarLayout();
				auto elementTypeLayout = typeLayout->getElementTypeLayout();
				//ParameterCategory elementCategory = elementVariable->getCategory();
				auto elementCategories =  UnwrapCategories(elementVariable);
	

				cacore::NameHash typeName = GetFullTypeName(elementTypeLayout);
				if (typeName == CANAME("ImguiContext"))
				{
					CA_LOG(" Found!");
				}
				if (parentHierarchyID == -1)
				{
					name = RootName();
					typeName = RootTypeName();
				}
				int32_t currentHierarchyID = bindingInfo.NewHierarchy(parentHierarchyID, name, typeName, elementCount);
				auto& currentHierarchy = bindingInfo.GetHierarchy(currentHierarchyID);
				{
					//if (accessPath.GetLeafSpaceAndBinding(slang::ParameterCategory::ConstantBuffer))
					{
						uint32_t stride = elementTypeLayout->getStride(slang::Uniform);
						if (stride > 0)
						{
							slang::ParameterCategory cbufferCategory = slang::ParameterCategory::ConstantBuffer;
							for (auto cat : elementCategories)
							{
								if (cat == slang::ParameterCategory::DescriptorTableSlot)
								{
									cbufferCategory = cat;
									break;
								}
							}

							SpaceAndBinding bindings{};
							accessPath.GetLastBufferBinding(bindings);
							currentHierarchy.m_SelfUniformBufferID = bindings.offset;
							currentHierarchy.m_SelfUniformSpaceID = bindings.space;

							currentHierarchy.m_SelfUniformUsage = CollectResourceUsage((SlangParameterCategory)cbufferCategory, bindings, metaDatas);
							auto& spaceInfo = bindingInfo.EnsureSpaceInfo(bindings.space, currentHierarchyID);
							spaceInfo.m_ResourceStats.m_CBufferBindings.push_back({ bindings.offset, elementCount, stride });
							spaceInfo.m_ResourceStats.m_TotalBindingCount++;
							spaceInfo.m_ResourceStats.m_CBufferCount += elementCount;
							CA_LOG("[{}]{} uniformBuffer space: {} binding: {} stride: {} arrayLength: {} category: {}", typeName, bindings.name, bindings.space, bindings.offset, stride, elementCount, GetCategoryName(cbufferCategory));
							CA_LOG("ConstBuffer [{}] {}", typeName, LogUsage(currentHierarchy.m_SelfUniformUsage));
						}
					}
				}

				AccessPathNode elementNode = accessPath.NewNode(elementVariable);
				accessPath.SetLeaf(elementNode);
				auto elementKind = elementTypeLayout->getKind();
				if (elementKind == slang::TypeReflection::Kind::Struct)
				{
					unsigned fieldCount = elementTypeLayout->getFieldCount();

					for (uint32_t i = 0; i < fieldCount; i++)
					{
						slang::VariableLayoutReflection* field = elementTypeLayout->getFieldByIndex(i);
						ReflectBindings(bindingInfo, field, accessPath, currentHierarchyID, metaDatas);
					}
				}
			}
		}

		void ReflectBindings(ShaderBindingInfo& bindingInfo
			, VariableLayoutReflection* variable, AccessPath accessPath
			, int32_t parentHierarchyID
			, castl::vector<slang::IMetadata*> const& metaDatas)
		{

			slang::TypeLayoutReflection* typeLayout = GetTypeLayoutNonArray(variable);
			slang::TypeReflection::Kind kind = typeLayout->getKind();

			if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock)
			{
				ReflectConstantBufferBindings(bindingInfo, variable, accessPath, parentHierarchyID, metaDatas);
				return;
			}


			AccessPathNode newNode = accessPath.NewNode(variable);
			accessPath.SetLeaf(newNode);
			cacore::NameHash typeName = GetFullTypeName(typeLayout);
			cacore::NameHash name = variable->getName();
			ParameterCategory variableCategory = variable->getCategory();
			uint32_t elementCount = GetArrayElementCount(variable->getTypeLayout());

			//assert(parentHierarchyID >= 0 && "Parent Hierarchy Should Be Valid");
			if (parentHierarchyID == -1)
			{
				assert(!typeName.Valid());
				assert(!name.Valid());
				name = RootName();
				typeName = RootTypeName();
				//if parent hierarchy is not valid, create a root hierarchy
				//parentHierarchyID = bindingInfo.NewHierarchy(parentHierarchyID, RootName(), RootTypeName(), 1);
			}

			if (kind == slang::TypeReflection::Kind::Struct)
			{
				int32_t currentHierarchyID = bindingInfo.NewHierarchy(parentHierarchyID, name, typeName, elementCount);

				unsigned fieldCount = typeLayout->getFieldCount();
				for (uint32_t i = 0; i < fieldCount; i++)
				{
					slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
					ReflectBindings(bindingInfo, field, accessPath, currentHierarchyID, metaDatas);
				}
			}
			else if (kind == slang::TypeReflection::Kind::Resource
				|| kind == slang::TypeReflection::Kind::SamplerState)
			{
				slang::BindingType bindingType = typeLayout->getBindingRangeType(0);
				SlangResourceAccess resourceAccess = typeLayout->getResourceAccess();
				//CA_BREAK_IF(name == CANAME("IMGUITextureSampler"));
				auto bindings = accessPath.GetLeafSpaceAndBinding();
				auto& parentHierarchy = bindingInfo.GetHierarchy(parentHierarchyID);
				auto& spaceInfo = bindingInfo.EnsureSpaceInfo(bindings.space, parentHierarchyID);

				ShaderResourceBinding newBinding = {};
				newBinding.m_TypeName = typeName;
				newBinding.m_Name = name;
				newBinding.m_ElementCount = elementCount;
				newBinding.m_BindingSpace = bindings.space;
				newBinding.m_BindingID = bindings.offset;
				newBinding.m_Access = TranslateSlangResourceAccess(resourceAccess);
				newBinding.m_Usage = CollectResourceUsage((SlangParameterCategory)variableCategory, bindings, metaDatas);
				CA_LOG("Resource[{}] Usage Mask{}", name, newBinding.m_Usage);

				switch (bindingType)
				{
				case slang::BindingType::MutableTexture:
				{
					spaceInfo.m_ResourceStats.m_RWBufferBindings.push_back({ bindings.offset, elementCount });
					spaceInfo.m_ResourceStats.m_TotalBindingCount++;
					spaceInfo.m_ResourceStats.m_RWTextureCount += elementCount;
					newBinding.m_ResourceType = EShaderResourceType::eRWTexture;
					break;
				}
				case slang::BindingType::Texture:
				{
					spaceInfo.m_ResourceStats.m_TextureBindings.push_back({ bindings.offset, elementCount });
					spaceInfo.m_ResourceStats.m_TotalBindingCount++;
					spaceInfo.m_ResourceStats.m_TextureCount += elementCount;
					newBinding.m_ResourceType = EShaderResourceType::eTexture;
					CA_LOG("[{}]{} texture space: {} binding: {} arrayLength: {} category: {}\n", typeName.c_str(), bindings.name.c_str(), bindings.space, bindings.offset, elementCount, GetCategoryName(variableCategory));
					break;
				}
				case slang::BindingType::MutableRawBuffer:
				{
					spaceInfo.m_ResourceStats.m_RWBufferBindings.push_back({ bindings.offset, elementCount });
					spaceInfo.m_ResourceStats.m_TotalBindingCount++;
					spaceInfo.m_ResourceStats.m_RWBufferCount+= elementCount;
					newBinding.m_ResourceType = EShaderResourceType::eRWStructuredBuffer;
					break;
				}
				case slang::BindingType::RawBuffer:
				{
					spaceInfo.m_ResourceStats.m_StorageBufferBindings.push_back({ bindings.offset, elementCount });
					spaceInfo.m_ResourceStats.m_TotalBindingCount++;
					spaceInfo.m_ResourceStats.m_StorageBufferCount += elementCount;
					newBinding.m_ResourceType = EShaderResourceType::eStructuredBuffer;
					CA_LOG("[{}]{} buffer space: {} binding: {} arrayLength: {} category: {}\n", typeName.c_str(), bindings.name.c_str(), bindings.space, bindings.offset, elementCount, GetCategoryName(variableCategory));
					break;
				}
				case slang::BindingType::Sampler:
				{
					spaceInfo.m_ResourceStats.m_SamplerBindings.push_back({ bindings.offset, elementCount });
					spaceInfo.m_ResourceStats.m_TotalBindingCount++;
					spaceInfo.m_ResourceStats.m_SamplerCount += elementCount;
					newBinding.m_ResourceType = EShaderResourceType::eSampler;
					CA_LOG("[{}]{} sampler space: {} binding: {} arrayLength: {} category: {}\n", typeName.c_str(), bindings.name.c_str(), bindings.space, bindings.offset, elementCount, GetCategoryName(variableCategory));
					break;
				}
				default:
					CA_LOG_ERR_BREAK("Unknown Binding Type");
					break;
				}

				CA_LOG(" {}", LogUsage(newBinding.m_Usage));

				parentHierarchy.m_Bindings.push_back(newBinding);
			}
		}

		static cacore::NameHash GetFullTypeName(TypeLayoutReflection* typeLayout)
		{
			auto varType = typeLayout->getType();
			cacore::NameHash resultTypeName = varType->getName();
			Slang::ComPtr<ISlangBlob> fullName;
			varType->getFullName(fullName.writeRef());
			if (fullName.get() != nullptr)
			{
				return static_cast<const char*>(fullName->getBufferPointer());
			}
			return {};
		}

		void ReflectTypeLayouts(ShaderReflectionData& reflectionData
			, slang::VariableLayoutReflection* variable
			, cacore::NameHash const& parentTypeName)
		{
			auto targetVariable = variable;
			slang::TypeLayoutReflection* typeLayout = GetTypeLayoutNonArray(variable);

			slang::TypeReflection::Kind kind = typeLayout->getKind();
			cacore::NameHash name = targetVariable->getName();
			auto categories = UnwrapCategories(targetVariable);
			auto& parentStruct = reflectionData.EnsureStruct(parentTypeName);
			uint32_t elementCount = GetArrayElementCount(variable->getTypeLayout());

			//Binding Done! Now Reflect By Kind

			//如果是ConstantBuffer或者ParameterBlock类型，需要追踪ElementVarLayout
			if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock)
			{
				targetVariable = typeLayout->getElementVarLayout();
				typeLayout = targetVariable->getTypeLayout();
				kind = typeLayout->getKind();
			}

			cacore::NameHash typeName = GetFullTypeName(typeLayout);

			if (kind == slang::TypeReflection::Kind::Struct)
			{
				//第一次追踪Struct类型中的Uniform成员时，创建UniformGroup
				auto& shaderStruct = reflectionData.EnsureStruct(typeName);
				uint32_t strideInBytes = typeLayout->getStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
				uint32_t sizeInBytes = typeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
				uint32_t offsetInBytes = targetVariable->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM);
				shaderStruct.m_StructUniforms.SetSize(sizeInBytes, strideInBytes);

				SubStructReference newSubStruct = {};
				newSubStruct.m_StructTypeName = typeName;
				newSubStruct.m_Name = name;
				newSubStruct.m_ElementCount = elementCount;
				newSubStruct.m_Stride = strideInBytes;
				newSubStruct.m_MemoryOffset = offsetInBytes;
				newSubStruct.m_ElementMemorySize = sizeInBytes;
				parentStruct.EnsureSubStructReference(newSubStruct);

				//for (auto category : categories)
				//{
				//	if (category == ParameterCategory::Uniform)
				//	{
				//		UniformElement newElement = {};
				//		newElement.Init(typeName, name
				//			, offsetInBytes, sizeInBytes, strideInBytes, elementCount);
				//		parentStruct.m_StructUniforms.EnsureElement(newElement);
				//	}
				//	else
				//	{
				//	}
				//}
				unsigned fieldCount = typeLayout->getFieldCount();
				for (uint32_t i = 0; i < fieldCount; i++)
				{
					slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
					ReflectTypeLayouts(reflectionData, field, typeName);
				}
			}
			else if (kind == slang::TypeReflection::Kind::Scalar
				|| kind == slang::TypeReflection::Kind::Vector
				|| kind == slang::TypeReflection::Kind::Matrix)
			{
				uint32_t strideInBytes = typeLayout->getStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
				uint32_t sizeInBytes = typeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
				uint32_t offsetInBytes = targetVariable->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM);
				UniformElement newElement = {};
				newElement.Init(typeName, name, offsetInBytes, sizeInBytes, strideInBytes, elementCount);
				parentStruct.m_StructUniforms.EnsureElement(newElement);
			}
			else if (kind == slang::TypeReflection::Kind::Resource
				|| kind == slang::TypeReflection::Kind::SamplerState)
			{
				slang::BindingType bindingType = typeLayout->getBindingRangeType(0);
				SlangResourceAccess resourceAccess = typeLayout->getResourceAccess();
				switch (bindingType)
				{
					case slang::BindingType::MutableTexture:
					case slang::BindingType::Texture:
					{
						ShaderTextureData shaderTextureData = {};
						shaderTextureData.m_Name = name;
						shaderTextureData.m_RWType = TranslateSlangResourceAccess(resourceAccess);
						shaderTextureData.m_ElementCount = elementCount;
						parentStruct.EnsureShaderTexture(shaderTextureData);
						break;
					}
					case slang::BindingType::MutableRawBuffer:
					case slang::BindingType::RawBuffer:
					{
						BufferData shaderBufferData = {};
						shaderBufferData.m_Name = name;
						shaderBufferData.m_RWType = TranslateSlangResourceAccess(resourceAccess);
						shaderBufferData.m_ElementCount = elementCount;
						parentStruct.EnsureShaderBuffer(shaderBufferData);
						break;
					}
					case slang::BindingType::Sampler:
					{
						parentStruct.EnsureTextureSampler(name);
						break;
					}
				}
			}
		}

		void  ReflectRootTypeLayouts(ShaderReflectionData& reflectionData
			, slang::VariableLayoutReflection* variable
			, cacore::NameHash const& rootTypeName)
		{
			auto targetVariable = variable;
			slang::TypeLayoutReflection* typeLayout = GetTypeLayoutNonArray(targetVariable);
			auto varType = variable->getType();
			cacore::NameHash resultTypeName = varType->getName();
			slang::TypeReflection::Kind kind = typeLayout->getKind();
			auto categories = UnwrapCategories(targetVariable);
			uint32_t elementCount = GetArrayElementCount(targetVariable->getTypeLayout());
			assert(elementCount == 1 && "Root Type Should Only Has One Element");

			//如果是ConstantBuffer或者ParameterBlock类型，需要追踪ElementVarLayout
			if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock)
			{
				targetVariable = typeLayout->getElementVarLayout();
				typeLayout = targetVariable->getTypeLayout();
				kind = typeLayout->getKind();
			}
			cacore::NameHash typeName = GetFullTypeName(typeLayout);
			CA_ASSERT_BREAK(!typeName.Valid(), "Root Type Name Should Not Be Valid");
			typeName = rootTypeName;

			CA_ASSERT_BREAK(kind == slang::TypeReflection::Kind::Struct, "Root Type Should Be Struct");
			if (kind == slang::TypeReflection::Kind::Struct)
			{
				//第一次追踪Struct类型中的Uniform成员时，创建UniformGroup
				auto& shaderStruct = reflectionData.EnsureStruct(typeName);
				uint32_t strideInBytes = typeLayout->getStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
				uint32_t sizeInBytes = typeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
				shaderStruct.m_StructUniforms.SetSize(sizeInBytes, strideInBytes);

				unsigned fieldCount = typeLayout->getFieldCount();
				for (uint32_t i = 0; i < fieldCount; i++)
				{
					slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
					ReflectTypeLayouts(reflectionData, field, typeName);
				}
			}
		}

		//TODO:Deprecated
		void Reflect(ShaderReflectionData& reflectionData
			, slang::VariableLayoutReflection* variable
			, ParameterCategory variableCategory
			, BindingData const& bindingData
			, uint32_t parentArrayLength)
		{
			return;
			slang::TypeLayoutReflection* typeLayout = variable->getTypeLayout();
			slang::TypeReflection::Kind kind = typeLayout->getKind();
			//处理混合类型
			if(variableCategory == ParameterCategory::Mixed)
			{
				unsigned categoryCount = variable->getCategoryCount();
				for (unsigned cc = 0; cc < categoryCount; cc++)
				{
					slang::ParameterCategory subCategory = variable->getCategoryByIndex(cc);
					Reflect(reflectionData, variable, subCategory, bindingData, parentArrayLength);
				}
				return;
			}
			//到这里时不应该有Mixed类型
			BindingData newBinding = OffsetBindingDataBySingleCategory(bindingData, variable, variableCategory);

			ShaderBindingSpaceData& bindingSpace = reflectionData.EnsureBindingSpace(newBinding.bindingSpace);

			//Binding Done! Now Reflect By Kind
			uint32_t elementCount = 1;
			//如果是Array类型，重定向为Array元素类型
			if (kind == slang::TypeReflection::Kind::Array)
			{
				elementCount = typeLayout->getElementCount();
				typeLayout = typeLayout->getElementTypeLayout();
				kind = typeLayout->getKind();
			}

			uint32_t unrolledArrayLength = parentArrayLength * elementCount;

			if (kind == slang::TypeReflection::Kind::ConstantBuffer)
			{
				auto elementVarLayout = typeLayout->getElementVarLayout();
				auto categories = UnwrapCategories(elementVarLayout);
				for (auto elementCategory : categories)
				{
					//在SpirV概念下，Uniform是
					bool uniformInConstantBuffer = (elementCategory == ParameterCategory::Uniform) && MayContainsUniform(variableCategory);
					if (elementCategory == variableCategory || uniformInConstantBuffer)
					{
						Reflect(reflectionData, elementVarLayout, elementCategory, newBinding, unrolledArrayLength);
					}
				}
			}
			if (kind == slang::TypeReflection::Kind::ParameterBlock)
			{
				auto elementVarLayout = typeLayout->getElementVarLayout();
				auto categories = UnwrapCategories(elementVarLayout);
				for (auto elementCategory : categories)
				{
					Reflect(reflectionData, elementVarLayout, elementCategory, newBinding, unrolledArrayLength);
				}
			}
			else if (kind == slang::TypeReflection::Kind::Struct)
			{
				//第一次追踪Struct类型中的Uniform成员时，创建UniformGroup
				if (variableCategory == ParameterCategory::Uniform)
				{
					uint32_t strideInBytes = typeLayout->getStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
					uint32_t sizeInBytes = typeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
					auto typeName = GetFullTypeName(typeLayout);
					newBinding.uniformGroupID = bindingSpace.InitUniformGroup(newBinding.bindingIndex
						, newBinding.uniformGroupID
						, newBinding.elementName, newBinding.memoryByteOffset, sizeInBytes, strideInBytes, elementCount);
					CA_LOG("[{}]{} space: {} binding: {} arrayLength: {} category: {}\n", typeName, newBinding.path.c_str(), newBinding.bindingSpace, newBinding.bindingIndex, elementCount, GetCategoryName(variableCategory));
				}
				else
				{
					auto typeName = GetFullTypeName(typeLayout);
					newBinding.resourceGroupID = bindingSpace.InitResourceGroup(newBinding.elementName, newBinding.resourceGroupID);
					CA_LOG("[{}]{} resource space: {} binding: {} arrayLength: {} category: {}\n", typeName, newBinding.path.c_str(), newBinding.bindingSpace, newBinding.bindingIndex, elementCount, GetCategoryName(variableCategory));
				}

				unsigned fieldCount = typeLayout->getFieldCount();
				for (uint32_t i = 0; i < fieldCount; i++)
				{
					slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
					auto categories = UnwrapCategories(field);
					for (auto fieldCategory : categories)
					{
						if (CategoryCheck(fieldCategory, variableCategory))
						{
							Reflect(reflectionData, field, fieldCategory, newBinding, unrolledArrayLength);
						}
					}
				}
			}
			else if (kind == slang::TypeReflection::Kind::Scalar
				|| kind == slang::TypeReflection::Kind::Vector
				|| kind == slang::TypeReflection::Kind::Matrix)
			{
				CA_ASSERT(variableCategory == ParameterCategory::Uniform, "Scalar, Vector, Matrix must be in Uniform Group");
				CA_ASSERT(newBinding.uniformGroupID >= 0, " A Uniform Group Must Be Created");
				uint32_t strideInBytes = typeLayout->getStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
				uint32_t sizeInBytes = typeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
				UniformElement newElement = {};
				auto typeName = GetFullTypeName(typeLayout);
				newElement.Init(typeName, newBinding.elementName, newBinding.memoryByteOffset, sizeInBytes, strideInBytes, elementCount);
				bindingSpace.AddElementToGroup(newBinding.bindingIndex, newBinding.uniformGroupID, newElement);
				CA_LOG("{} space: {} binding: {} arrayLength: {} category: {}\n", newBinding.path.c_str(), newBinding.bindingSpace, newBinding.bindingIndex, elementCount, GetCategoryName(variableCategory));
			}
			else if (kind == slang::TypeReflection::Kind::Resource
				|| kind == slang::TypeReflection::Kind::SamplerState)
			{
				slang::BindingType bindingType = typeLayout->getBindingRangeType(0);
				SlangResourceAccess resourceAccess = typeLayout->getResourceAccess();
				switch (bindingType)
				{
					case slang::BindingType::MutableTexture:
					case slang::BindingType::Texture:
					{
						TextureData newTexture = {};
						newTexture.m_Name = newBinding.elementName;
						newTexture.m_BindingIndex = newBinding.bindingIndex;
						newTexture.m_Count = unrolledArrayLength;
						newTexture.m_Access = TranslateSlangResourceAccess(resourceAccess);
						bindingSpace.AddTextureToResourceGroup(newBinding.resourceGroupID, newTexture);
						break;
					}
					case slang::BindingType::MutableRawBuffer:
					case slang::BindingType::RawBuffer:
					{
						ShaderBufferData newBuffer = {};
						newBuffer.m_Name = newBinding.elementName;
						newBuffer.m_BindingIndex = newBinding.bindingIndex;
						newBuffer.m_Count = unrolledArrayLength;
						newBuffer.m_Access = TranslateSlangResourceAccess(resourceAccess);
						bindingSpace.AddBufferToResourceGroup(newBinding.resourceGroupID, newBuffer);
						break;
					}
					case slang::BindingType::Sampler:
					{
						SamplerData newSampler = {};
						newSampler.m_Name = newBinding.elementName;
						newSampler.m_BindingIndex = newBinding.bindingIndex;
						newSampler.m_Count = unrolledArrayLength;
						bindingSpace.AddSamplerToResourceGroup(newBinding.resourceGroupID, newSampler);
						break;
					}
				}
				CA_LOG("{} bindingType: {} space: {} binding: {} arrayLength: {} category: {}\n", newBinding.path.c_str(), GetBindingTypeName(bindingType), newBinding.bindingSpace, newBinding.bindingIndex, unrolledArrayLength, GetCategoryName(variableCategory));
			}
		}

		void ReflectVertexAttributes(castl::vector<ShaderVertexAttributeData>& results, slang::VariableLayoutReflection* param, uint32_t locationOffset)
		{
			if (param->getCategory() == ParameterCategory::VaryingInput)
			{
				auto typeLayout = param->getTypeLayout();
				auto kind = typeLayout->getKind();
				uint32_t location = param->getOffset(SLANG_PARAMETER_CATEGORY_VARYING_INPUT) + locationOffset;
				if (kind == slang::TypeReflection::Kind::Struct)
				{
					unsigned fieldCount = typeLayout->getFieldCount();
					for (uint32_t i = 0; i < fieldCount; i++)
					{
						slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
						ReflectVertexAttributes(results, field, location);
					}
				}
				else if (kind == slang::TypeReflection::Kind::Scalar
					|| kind == slang::TypeReflection::Kind::Vector
					|| kind == slang::TypeReflection::Kind::Matrix)
				{
					ShaderVertexAttributeData newAttribute = {};
					newAttribute.m_Location = location;
					newAttribute.m_Name = param->getName();
					newAttribute.m_SematicName = param->getSemanticName();
					newAttribute.m_SematicIndex = param->getSemanticIndex();
					results.push_back(newAttribute);
				}
			}
		}

		void DoCompile()
		{
			std::vector<const char*> searchPaths;
			searchPaths.resize(m_SearchPaths.size());
			for (int i = 0; i < m_SearchPaths.size(); ++i)
			{
				searchPaths[i] = m_SearchPaths[i].c_str();
			}
			std::vector<PreprocessorMacroDesc> macroNames;
			macroNames.reserve(m_Macros.size());
			for (auto& pair : m_Macros)
			{
				if(pair.second.empty())
					continue;
				PreprocessorMacroDesc macroDesc = {};
				macroDesc.name = pair.first.c_str();
				macroDesc.value = pair.second.c_str();
				macroNames.push_back(macroDesc);
			}

			m_CompileSessionDesc.targetCount = m_TargetDescs.size();
			m_CompileSessionDesc.targets = m_TargetDescs.data();
			m_CompileSessionDesc.searchPathCount = searchPaths.size();
			m_CompileSessionDesc.searchPaths = searchPaths.data();
			m_CompileSessionDesc.preprocessorMacroCount = macroNames.size();
			m_CompileSessionDesc.preprocessorMacros = macroNames.data();
			m_CompileSessionDesc.compilerOptionEntryCount = m_CompilerOptionEntries.size();
			m_CompileSessionDesc.compilerOptionEntries = m_CompilerOptionEntries.data();

			Slang::ComPtr<ISlangBlob> diagnostics;
			//Initialize Session
			m_Session->createSession(m_CompileSessionDesc, m_CompileSession.writeRef());
			std::vector<IComponentType*> components;
			components.reserve(m_ModuleNames.size());
			for (auto& module : m_ModuleNames)
			{
				auto imodule = m_CompileSession->loadModule(module.c_str(), diagnostics.writeRef());
				if (diagnostics)
				{
					CA_LOG_ERR((const char*)diagnostics->getBufferPointer());
					m_ErrorList.push_back((const char*)diagnostics->getBufferPointer());
					diagnostics.setNull();
				}
				components.push_back(imodule);
				int moduleEntryPointCount = imodule->getDefinedEntryPointCount();
				for (int i = 0; i < moduleEntryPointCount; ++i)
				{
					Slang::ComPtr<IEntryPoint> itrEntryPoint;
					imodule->getDefinedEntryPoint(i, itrEntryPoint.writeRef());
					components.push_back(itrEntryPoint.get());
				}
			}
			Slang::ComPtr<IComponentType> program;
			m_CompileSession->createCompositeComponentType(components.data(), components.size(), program.writeRef());

			Slang::ComPtr<IComponentType> linkedProgram;
			program->link(linkedProgram.writeRef(), diagnostics.writeRef());
			if (diagnostics)
			{
				CA_LOG_ERR((const char*)diagnostics->getBufferPointer());
				m_ErrorList.push_back((const char*)diagnostics->getBufferPointer());
				diagnostics.setNull();
			}

			m_CompileResults.clear();
			m_CompileResults.reserve(m_TargetDescs.size());
			for (int targetIndex = 0; targetIndex < m_TargetDescs.size(); ++targetIndex)
			{
				ShaderCompileTargetResult outputTargetResult = {};
				switch (m_TargetDescs[targetIndex].format)
				{
					case SlangCompileTarget::SLANG_SPIRV:
					{
						outputTargetResult.targetType = EShaderTargetType::eSpirV;
						break;
					}
					case SlangCompileTarget::SLANG_DXIL:
					{
						outputTargetResult.targetType = EShaderTargetType::eDXIL;
						break;
					}
					case SlangCompileTarget::SLANG_HLSL:
					{
						outputTargetResult.targetType = EShaderTargetType::eHLSL;
						break;
					}
				}
				slang::ProgramLayout* layout = linkedProgram->getLayout(targetIndex, diagnostics.writeRef());
				if (diagnostics)
				{
					CA_LOG_ERR((const char*)diagnostics->getBufferPointer());
					m_ErrorList.push_back((const char*)diagnostics->getBufferPointer());
					diagnostics.setNull();
				}
				ShaderReflectionData reflectionData = {};

				int entryPointCount = layout->getEntryPointCount();
				outputTargetResult.programs.reserve(entryPointCount);
				EShaderTypeFlags shaderTypeFlags = 0;
				castl::vector<slang::IMetadata*> entryPointMetaDatas;
				entryPointMetaDatas.resize(entryPointCount);
				for (int entryPointIndex = 0; entryPointIndex < entryPointCount; ++entryPointIndex)
				{
					Slang::ComPtr<IBlob> kernelBlob;
					linkedProgram->getEntryPointCode(entryPointIndex, targetIndex, kernelBlob.writeRef(), diagnostics.writeRef());
					if (diagnostics)
					{
						CA_LOG_ERR((const char*)diagnostics->getBufferPointer());
						m_ErrorList.push_back((const char*)diagnostics->getBufferPointer());
						diagnostics.setNull();
					}
					ShaderProgramData outProgramData = {};
					linkedProgram->getEntryPointMetadata(
						entryPointIndex,
						targetIndex,
						&entryPointMetaDatas[entryPointIndex]
						, diagnostics.writeRef());
					if (diagnostics)
					{
						CA_LOG_ERR((const char*)diagnostics->getBufferPointer());
						m_ErrorList.push_back((const char*)diagnostics->getBufferPointer());
						diagnostics.setNull();
					}

					auto entryPointRef = layout->getEntryPointByIndex(entryPointIndex);
					auto shaderStage = entryPointRef->getStage();
					switch (shaderStage)
					{
					case SLANG_STAGE_NONE:
						outProgramData.shaderType = ECompileShaderType::eMax;
						break;
					case SLANG_STAGE_VERTEX:
						outProgramData.shaderType = ECompileShaderType::eVert;
						shaderTypeFlags |= EShaderTypeMask::eVert;
						break;
					case SLANG_STAGE_HULL:
						outProgramData.shaderType = ECompileShaderType::eTessCtr;
						shaderTypeFlags |= EShaderTypeMask::eTessCtr;
						break;
					case SLANG_STAGE_DOMAIN:
						outProgramData.shaderType = ECompileShaderType::eTessEvl;
						shaderTypeFlags |= EShaderTypeMask::eTessEvl;
						break;
					case SLANG_STAGE_GEOMETRY:
						outProgramData.shaderType = ECompileShaderType::eGeom;
						shaderTypeFlags |= EShaderTypeMask::eGeom;
						break;
					case SLANG_STAGE_FRAGMENT:
						outProgramData.shaderType = ECompileShaderType::eFrag;
						shaderTypeFlags |= EShaderTypeMask::eFrag;
						break;
					case SLANG_STAGE_COMPUTE:
						outProgramData.shaderType = ECompileShaderType::eComp;
						shaderTypeFlags |= EShaderTypeMask::eComp;
						break;
					case SLANG_STAGE_RAY_GENERATION:
						outProgramData.shaderType = ECompileShaderType::eRaygen;
						shaderTypeFlags |= EShaderTypeMask::eRaygen;
						break;
					case SLANG_STAGE_INTERSECTION:
						outProgramData.shaderType = ECompileShaderType::eIntersect;
						shaderTypeFlags |= EShaderTypeMask::eIntersect;
						break;
					case SLANG_STAGE_ANY_HIT:
						outProgramData.shaderType = ECompileShaderType::eAnyhit;
						shaderTypeFlags |= EShaderTypeMask::eAnyhit;
						break;
					case SLANG_STAGE_CLOSEST_HIT:
						outProgramData.shaderType = ECompileShaderType::eClosehit;
						shaderTypeFlags |= EShaderTypeMask::eClosehit;
						break;
					case SLANG_STAGE_MISS:
						outProgramData.shaderType = ECompileShaderType::eMiss;
						shaderTypeFlags |= EShaderTypeMask::eMiss;
						break;
					case SLANG_STAGE_CALLABLE:
						outProgramData.shaderType = ECompileShaderType::eCallable;
						shaderTypeFlags |= EShaderTypeMask::eCallable;
						break;
					case SLANG_STAGE_MESH:
						outProgramData.shaderType = ECompileShaderType::eMesh;
						shaderTypeFlags |= EShaderTypeMask::eMesh;
						break;
					case SLANG_STAGE_AMPLIFICATION:
						outProgramData.shaderType = ECompileShaderType::eAmplification;
						shaderTypeFlags |= EShaderTypeMask::eAmplification;
						break;
					default:
						outProgramData.shaderType = ECompileShaderType::eMax;
						break;
					}
					


					outProgramData.entryPointName = entryPointRef->getName();
					outProgramData.data.resize(kernelBlob->getBufferSize());
					memcpy(outProgramData.data.data(), kernelBlob->getBufferPointer(), kernelBlob->getBufferSize());

					//CA_LOG("Debug: {}\n", (char*)outProgramData.data.data());
					outputTargetResult.programs.push_back(outProgramData);

					if (outProgramData.shaderType == ECompileShaderType::eVert)
					{
						int paramCount = entryPointRef->getParameterCount();
						for (int entryPointParamIndex = 0; entryPointParamIndex < paramCount; ++entryPointParamIndex)
						{
							auto param = entryPointRef->getParameterByIndex(entryPointParamIndex);
							if (param->getCategory() == ParameterCategory::VaryingInput)
							{
								ReflectVertexAttributes(reflectionData.m_VertexAttributes, param, 0);
							}
						}
					}
				}


				BindingData bindingData;
				SpaceAndBindingOffset bindingAndOffsets = {};
				AccessPath accessPath = {};


				{
					auto globalParamVarLayout = layout->getGlobalParamsVarLayout();

					ReflectRootTypeLayouts(reflectionData, globalParamVarLayout, RootName());
					ReflectBindings(reflectionData.m_BindingInfo
						, globalParamVarLayout
						, accessPath
						, -1
						, entryPointMetaDatas);
				}


				{
					size_t globalBufferSize = layout->getGlobalConstantBufferSize();
					uint32_t globalBinding = layout->getGlobalConstantBufferBinding();
					auto& globalBindingSpace = reflectionData.EnsureBindingSpace(globalBinding);
					if (globalBufferSize > 0)
					{
						bindingData.uniformGroupID = globalBindingSpace.InitUniformGroup(0, -1, "__Global", 0, globalBufferSize, globalBufferSize, 1);
					}
				}
				uint32_t paramCount = layout->getParameterCount();
				for (uint32_t paramID = 0; paramID < paramCount; ++paramID)
				{
					auto param = layout->getParameterByIndex(paramID);
					Reflect(reflectionData, param, param->getCategory(), bindingData, 1);
				}
				outputTargetResult.m_ReflectionData = reflectionData;
				outputTargetResult.shaderTypeFlags = shaderTypeFlags;
				m_CompileResults.push_back(outputTargetResult);
			}
		}

		void ClearCompileTask()
		{
			m_CompileSessionDesc = {};
			m_CompileSession.setNull();
			m_TargetDescs.clear();
			m_SearchPaths.clear();
			m_Macros.clear();
			m_ModuleNames.clear();
			m_ErrorList.clear();
			m_CompileResults.clear();
		}

	};

	class ShaderCompilerManager : public IShaderCompilerManager
	{
	public:

		~ShaderCompilerManager()
		{
			for(auto& compiler : m_Compilers)
			{
				compiler.Release();
			}
			m_Compilers.clear();
			m_AvailableCompilers.clear();
		}

		void Init(cacore::IModuleManager* pManger)
		{
			InitializePoolSize(2);
		}

		virtual IShaderCompiler* AquireShaderCompiler() override
		{
			castl::unique_lock<castl::mutex> lock(m_Mutex);
			m_ConditinalVariable.wait(lock, [this]()
				{
					return !m_AvailableCompilers.empty();
				});
			IShaderCompiler* compiler = m_AvailableCompilers.back();
			m_AvailableCompilers.pop_back();
			return compiler;
		}
		virtual void ReturnShaderCompiler(IShaderCompiler* compiler) override
		{
			{
				castl::lock_guard<castl::mutex> lock(m_Mutex);
				m_AvailableCompilers.push_back(compiler);
			}
			m_ConditinalVariable.notify_one();
		}

		virtual void InitializePoolSize(uint32_t compiler_count) override
		{
			castl::lock_guard<castl::mutex> lock(m_Mutex);
			if (m_Compilers.empty())
			{
				m_Compilers.resize(compiler_count);
				m_AvailableCompilers.resize(compiler_count);
				for (uint32_t i = 0; i < compiler_count; ++i)
				{
					m_Compilers[i].Init();
					m_AvailableCompilers[i] = &m_Compilers[i];
				}
			}
		}

		castl::condition_variable m_ConditinalVariable;
		castl::mutex m_Mutex;
		castl::vector<IShaderCompiler*> m_AvailableCompilers;
		castl::vector<Compiler_Impl> m_Compilers;
	};
	//CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(IShaderCompilerManager, ShaderCompilerManager);
}

CA_MODULE_INSTANCE(ShaderCompilerSlang::IShaderCompilerManager, ShaderCompilerSlang::ShaderCompilerManager, ShaderCompilerManager_Slang);

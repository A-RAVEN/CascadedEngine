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
#include <LibraryExportCommon.h>

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
				PushTarget(SLANG_GLSL, "glsl_450");
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
			CompilerOptionEntry{ CompilerOptionName::Optimization , CompilerOptionValue{ CompilerOptionValueKind::Int, SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_NONE }}
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

		struct BindingInfo
		{
			castl::string name = "";

			struct CategoryBindingInfo
			{
				uint32_t bindingSpace = 0;
				uint32_t bindingPos = 0;
			};
			castl::map<slang::ParameterCategory, CategoryBindingInfo> categoryOffsets;

			uint32_t GetBindingSpace(slang::ParameterCategory category) const
			{
				auto found = categoryOffsets.find(category);
				if(found != categoryOffsets.end())
				{
					return found->second.bindingSpace;
				}
				return 0u;
			}

			uint32_t GetUniformMemoryOffset() const
			{
				return GetBindingSpace(slang::ParameterCategory::Uniform);
			}

			uint32_t GetBindingPos(slang::ParameterCategory category) const
			{
				auto found = categoryOffsets.find(category);
				if (found != categoryOffsets.end())
				{
					return found->second.bindingPos;
				}
				return 0u;
			}

			BindingInfo OffsetVariableInfo(slang::VariableLayoutReflection* variable) const
			{
				BindingInfo result = *this;

				auto pName = variable->getName();
				if (pName != nullptr)
				{
					result.name += "." + castl::string(pName);
				}

				auto typeLayout = variable->getTypeLayout();
				auto category = variable->getCategory();
				castl::vector<slang::ParameterCategory> categories;
				if (category == ParameterCategory::Mixed)
				{
					unsigned categoryCount = variable->getCategoryCount();
					categories.reserve(categoryCount);
					for (unsigned cc = 0; cc < categoryCount; cc++)
					{
						slang::ParameterCategory subCategory = variable->getCategoryByIndex(cc);
						categories.push_back(subCategory);
					}
				}
				else
				{
					categories.push_back(category);
				}
				for (auto itrCategory : categories)
				{
					uint32_t bindingSpaceOffset = variable->getBindingSpace((SlangParameterCategory)itrCategory) + variable->getOffset(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE);
					uint32_t bindingPositionOffset = variable->getOffset((SlangParameterCategory)itrCategory);
					auto findItr = categoryOffsets.find(itrCategory);
					if(findItr == categoryOffsets.end())
					{
						result.categoryOffsets[itrCategory] = CategoryBindingInfo{ bindingSpaceOffset, bindingPositionOffset };
					}
					else
					{
						CategoryBindingInfo& bindingInfo = result.categoryOffsets[itrCategory];
						bindingInfo.bindingPos += bindingPositionOffset;
						bindingInfo.bindingSpace += bindingSpaceOffset;
					}
				}
				return result;
			}



			//BindingInfo OffsetBiasInfo(char const* varName, uint32_t byteOffset, uint32_t bindingIndexOffset, uint32_t bindingSpaceOffset) const
			//{
			//	BindingInfo result = *this;
			//	if (varName != nullptr)
			//	{
			//		result.name = result.name + "." + castl::string(varName);
			//	}
		
			//	return result;
			//}
		};



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

		//可能是ParameterBlock, ConstantBuffer, Array成员的类型
		void ReflectElementType(ShaderReflectionData& reflectionData
			, const char* containerName
			, uint32_t arrayLength
			, BindingInfo const& containerBindingInfo
			, int parentGroupID
			, slang::VariableLayoutReflection* thisVariable
			, slang::TypeLayoutReflection* typeLayout)
		{
			slang::TypeReflection::Kind kind = typeLayout->getKind();
			ParameterCategory elementCategory = thisVariable->getCategory();
			if (kind == slang::TypeReflection::Kind::Struct)
			{
				int newUniformGroupID = -1;
				//有UniformBuffer, 初始化UniformGroup
				uint32_t uniformBufferSize = typeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
				if (uniformBufferSize > 0)
				{
					uint32_t strideInBytes = typeLayout->getStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
					ShaderBindingSpaceData& bindingSpace = reflectionData.EnsureBindingSpace(containerBindingInfo.GetBindingSpace(ParameterCategory::Uniform));
					newUniformGroupID = bindingSpace.InitUniformGroup(containerBindingInfo.GetBindingPos(ParameterCategory::DescriptorTableSlot)
						, parentGroupID
						, containerName, containerBindingInfo.GetUniformMemoryOffset(), uniformBufferSize, strideInBytes, arrayLength);
					fprintf(stderr, "%s size: %d, stride: %d, offset: %d, length: %d, bindingIndex: %d, bindingSpace: %d\n"
						, containerBindingInfo.name.c_str(), (int)uniformBufferSize, (int)strideInBytes, (int)containerBindingInfo.GetUniformMemoryOffset(), (int)arrayLength, containerBindingInfo.GetBindingPos(ParameterCategory::DescriptorTableSlot), containerBindingInfo.GetBindingSpace(ParameterCategory::Uniform));
				}
				//TODO:初始化Resource相关容器，类似UniformGroup


				//for(ParameterCategory itrCategory : categories)
				{
					//uint32_t strideInBytes = typeLayout->getStride((SlangParameterCategory)itrCategory);
					//uint32_t sizeInBytes = typeLayout->getSize((SlangParameterCategory)itrCategory);
					//uint32_t bindingSpaceOffset = thisVariable->getBindingSpace((SlangParameterCategory)itrCategory) + thisVariable->getOffset(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE);
					//uint32_t bindingIndexOffset = (itrCategory == ParameterCategory::Uniform) ? 0u : thisVariable->getOffset((SlangParameterCategory)itrCategory);
					//uint32_t byteOffset = (itrCategory == ParameterCategory::Uniform) ? thisVariable->getOffset((SlangParameterCategory)itrCategory) : 0u;
					//BindingInfo thisBindingInfo = containerBindingInfo.OffsetBiasInfo(containerName, byteOffset, bindingIndexOffset, bindingSpaceOffset);
					//ShaderBindingSpaceData& bindingSpace = reflectionData.EnsureBindingSpace(thisBindingInfo.bindingSpace);


					//if (itrCategory == ParameterCategory::Uniform)
					//{
					//	newUniformGroupID = bindingSpace.InitUniformGroup(thisBindingInfo.bindingIndex
					//		, parentGroupID
					//		, containerName, thisBindingInfo.memoryPosition, sizeInBytes, strideInBytes, arrayLength);
					//	fprintf(stderr, "%s size: %d, stride: %d, offset: %d, length: %d, bindingIndex: %d, bindingSpace: %d\n"
					//		, thisBindingInfo.name.c_str(), (int)sizeInBytes, (int)strideInBytes, (int)thisBindingInfo.memoryPosition, (int)arrayLength, thisBindingInfo.bindingIndex, thisBindingInfo.bindingSpace);
					//}
					////TODO:初始化Resource相关容器，类似UniformGroup

					unsigned fieldCount = typeLayout->getFieldCount();
					for (uint32_t i = 0; i < fieldCount; i++)
					{
						slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
						ParameterCategory fieldCategory = field->getCategory();

						BindingInfo fieldBindingInfo = containerBindingInfo.OffsetVariableInfo(field);
						{
							ReflectVariable(fieldBindingInfo, reflectionData, newUniformGroupID, 1, field);
						}
					}
				}
			}
			else if (kind == slang::TypeReflection::Kind::Scalar || kind == slang::TypeReflection::Kind::Vector || kind == slang::TypeReflection::Kind::Matrix)
			{
				CA_ASSERT(elementCategory == ParameterCategory::Uniform, "Scalar, Vector, Matrix must be uniform category");
				uint32_t strideInBytes = typeLayout->getStride((SlangParameterCategory)ParameterCategory::Uniform);
				uint32_t sizeInBytes = typeLayout->getSize((SlangParameterCategory)ParameterCategory::Uniform);
				//uint32_t bindingSpaceOffset = thisVariable->getBindingSpace((SlangParameterCategory)elementCategory) + thisVariable->getOffset(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE);
				//uint32_t bindingIndexOffset = (elementCategory == ParameterCategory::Uniform) ? 0u : thisVariable->getOffset((SlangParameterCategory)elementCategory);
				//uint32_t byteOffset = (elementCategory == ParameterCategory::Uniform) ? thisVariable->getOffset((SlangParameterCategory)elementCategory) : 0u;
				BindingInfo thisBindingInfo = containerBindingInfo.OffsetVariableInfo(thisVariable);
				ShaderBindingSpaceData& bindingSpace = reflectionData.EnsureBindingSpace(thisBindingInfo.GetBindingSpace(elementCategory));

	
				UniformElement newElement = {};
				newElement.Init(containerName, thisBindingInfo.GetUniformMemoryOffset(), sizeInBytes, strideInBytes, arrayLength);
				bindingSpace.AddElementToGroup(thisBindingInfo.GetBindingPos(ParameterCategory::DescriptorTableSlot), parentGroupID, newElement);
				fprintf(stderr, "%s uniform size: %d, stride: %d, offset: %d, length: %d\n", thisBindingInfo.name.c_str(), (int)sizeInBytes, (int)strideInBytes, (int)thisBindingInfo.GetUniformMemoryOffset(), (int)arrayLength);
			}
			else if (kind == slang::TypeReflection::Kind::Resource)
			{
				//uint32_t bindingSpaceOffset = thisVariable->getBindingSpace((SlangParameterCategory)elementCategory) + thisVariable->getOffset(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE);
				//uint32_t bindingIndexOffset = (elementCategory == ParameterCategory::Uniform) ? 0u : thisVariable->getOffset((SlangParameterCategory)elementCategory);
				//uint32_t byteOffset = (elementCategory == ParameterCategory::Uniform) ? thisVariable->getOffset((SlangParameterCategory)elementCategory) : 0u;
				BindingInfo thisBindingInfo = containerBindingInfo.OffsetVariableInfo(thisVariable);
				ShaderBindingSpaceData& bindingSpace = reflectionData.EnsureBindingSpace(thisBindingInfo.GetBindingSpace(elementCategory));
				fprintf(stderr, "%s bindingIndex: %d, bindingSpace: %d,  length: %d\n", thisBindingInfo.name.c_str()
					, (int)containerBindingInfo.GetBindingPos(elementCategory), (int)thisBindingInfo.GetBindingSpace(elementCategory), (int)arrayLength);
			}
		}

		struct BindingData1
		{
			uint32_t bindingSpace = 0;
			uint32_t bindingIndex = 0;
			uint32_t memoryByteOffset = 0;
			BindingData1 OffsetSpace(uint32_t spaceOffset) const
			{
				BindingData1 newBinding = *this;
				newBinding.bindingSpace += spaceOffset;
				newBinding.bindingIndex = 0;
				newBinding.memoryByteOffset = 0;
				return newBinding;
			}
			BindingData1 OffsetBindingIndex(uint32_t indexOffset) const
			{
				BindingData1 newBinding = *this;
				newBinding.bindingIndex += indexOffset;
				return newBinding;
			}
			BindingData1 OffsetMemory(uint32_t bytesOffset) const
			{
				BindingData1 newBinding = *this;
				newBinding.memoryByteOffset += bytesOffset;
				return newBinding;
			}
		};

		void ReflectByKind(slang::VariableLayoutReflection* variable, slang::TypeLayoutReflection* typeLayout, ParameterCategory variableCategory, BindingData1 const& bindingData)
		{
			slang::TypeReflection::Kind kind = typeLayout->getKind();

			if (kind == slang::TypeReflection::Kind::ParameterBlock || kind == slang::TypeReflection::Kind::ConstantBuffer)
			{
				auto elementVarLayout = typeLayout->getElementVarLayout();
				auto elementCategory = elementVarLayout->getCategory();
				Reflect(elementVarLayout, elementCategory, bindingData);
			}
			else if (kind == slang::TypeReflection::Kind::Array)
			{
				auto elementTypeLayout = typeLayout->getElementTypeLayout();

			}
		}

		void Reflect(slang::VariableLayoutReflection* variable, ParameterCategory variableCategory, BindingData1 const& bindingData)
		{
			if(variableCategory == ParameterCategory::Mixed)
			{
				unsigned categoryCount = variable->getCategoryCount();
				for (unsigned cc = 0; cc < categoryCount; cc++)
				{
					slang::ParameterCategory subCategory = variable->getCategoryByIndex(cc);
					Reflect(variable, subCategory, bindingData);
				}
				return;
			}
			BindingData1 newBinding;
			if (variableCategory == ParameterCategory::SubElementRegisterSpace)
			{
				uint32_t bindingSpaceOffset = variable->getBindingSpace(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE) + variable->getOffset(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE);
				newBinding = bindingData.OffsetSpace(bindingSpaceOffset);
			}
			else if (variableCategory == ParameterCategory::Uniform)
			{
				newBinding = bindingData.OffsetMemory(variable->getOffset((SlangParameterCategory)variableCategory));
			}
			else
			{
				newBinding = bindingData.OffsetBindingIndex(variable->getOffset((SlangParameterCategory)variableCategory));
			}
			ReflectByKind(variable, variableCategory, newBinding);
		}


		void ReflectVariable(BindingInfo const& parentBias, ShaderReflectionData& reflectionData, int uniformGroupID, int arrayLength, slang::VariableLayoutReflection* variable)
		{
			slang::TypeLayoutReflection* typeLayout = variable->getTypeLayout();
			slang::TypeReflection::Kind kind = typeLayout->getKind();
			auto name = variable->getName();
			{
				//解包ParameterBlock,ConstantBuffer
				if (kind == slang::TypeReflection::Kind::ConstantBuffer)
				{
					ParameterCategory category = variable->getCategory();
					//uint32_t bindingSpaceOffset = variable->getBindingSpace((SlangParameterCategory)category) + variable->getOffset(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE);
					//uint32_t bindingIndexOffset = variable->getOffset((SlangParameterCategory)category);
					//uint32_t byteOffset = variable->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM);
					//BindingInfo containerBinding = parentBias.OffsetBiasInfo(nullptr, byteOffset, bindingIndexOffset, bindingSpaceOffset);
					auto elementTypeLayout = typeLayout->getElementTypeLayout();
					auto elementVarLayout = typeLayout->getElementVarLayout();
					//uint32_t uniformSize = elementTypeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
					//uint32_t uniformSize1 = typeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
					BindingInfo containerBinding = parentBias.OffsetVariableInfo(variable);
					fprintf(stderr, "%s constantbuffer category name: %s\n", (containerBinding.name + "." + castl::string(name)).c_str(), GetCategoryName(category));
					ReflectElementType(reflectionData, name, 1, containerBinding, uniformGroupID, elementVarLayout, elementTypeLayout);
				}
				if (kind == slang::TypeReflection::Kind::ParameterBlock)
				{
					ParameterCategory category = variable->getCategory();
					//CA_ASSERT(category != ParameterCategory::Mixed, "ParameterBlock can't be mixed category");
					//uint32_t bindingSpaceOffset = variable->getBindingSpace((SlangParameterCategory)category) + variable->getOffset(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE);
					//uint32_t bindingIndexOffset = variable->getOffset((SlangParameterCategory)category);
					//uint32_t byteOffset = variable->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM);
					//BindingInfo containerBinding = parentBias.OffsetBiasInfo(nullptr, byteOffset, bindingIndexOffset, bindingSpaceOffset);
					auto elementTypeLayout = typeLayout->getElementTypeLayout();
					auto elementVarLayout = typeLayout->getElementVarLayout();
					BindingInfo containerBinding = parentBias.OffsetVariableInfo(variable);
					fprintf(stderr, "%s parameterblock category name: %s\n", (containerBinding.name + "." + castl::string(name)).c_str(), GetCategoryName(category));
					ReflectElementType(reflectionData, name, 1, containerBinding, uniformGroupID, elementVarLayout, elementTypeLayout);
				}
				//解包Array
				else if (kind == slang::TypeReflection::Kind::Array)
				{
					int elementCount = typeLayout->getElementCount();
					auto elementTypeLayout = typeLayout->getElementTypeLayout();
					auto category = variable->getCategory();
					ReflectElementType(reflectionData, name, elementCount, parentBias, uniformGroupID, variable, elementTypeLayout);
				}
				//其它类型直接传递
				else if (kind == slang::TypeReflection::Kind::Struct
					|| kind == slang::TypeReflection::Kind::Scalar
					|| kind == slang::TypeReflection::Kind::Vector
					|| kind == slang::TypeReflection::Kind::Matrix
					|| kind == slang::TypeReflection::Kind::Resource)
				{
					//Reflect Type
					ReflectElementType(reflectionData, name, 1, parentBias, uniformGroupID, variable, typeLayout);
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
					fprintf(stderr, "%s\n", (const char*)diagnostics->getBufferPointer());
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
				fprintf(stderr, "%s\n", (const char*)diagnostics->getBufferPointer());
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
				}
				slang::ProgramLayout* layout = linkedProgram->getLayout(targetIndex, diagnostics.writeRef());
				if (diagnostics)
				{
					fprintf(stderr, "%s\n", (const char*)diagnostics->getBufferPointer());
					m_ErrorList.push_back((const char*)diagnostics->getBufferPointer());
					diagnostics.setNull();
				}
				int entryPointCount = layout->getEntryPointCount();
				outputTargetResult.programs.reserve(entryPointCount);
				for (int entryPointIndex = 0; entryPointIndex < entryPointCount; ++entryPointIndex)
				{
					Slang::ComPtr<IBlob> kernelBlob;
					linkedProgram->getEntryPointCode(entryPointIndex, targetIndex, kernelBlob.writeRef(), diagnostics.writeRef());
					if (diagnostics)
					{
						fprintf(stderr, "%s\n", (const char*)diagnostics->getBufferPointer());
						m_ErrorList.push_back((const char*)diagnostics->getBufferPointer());
						diagnostics.setNull();
					}
					ShaderProgramData outProgramData = {};

					auto entryPointRef = layout->getEntryPointByIndex(entryPointIndex);
					auto shaderStage = entryPointRef->getStage();
					switch (shaderStage)
					{
					case SLANG_STAGE_NONE:
						outProgramData.shaderType = ECompileShaderType::eMax;
						break;
					case SLANG_STAGE_VERTEX:
						outProgramData.shaderType = ECompileShaderType::eVert;
						break;
					case SLANG_STAGE_HULL:
						outProgramData.shaderType = ECompileShaderType::eTessCtr;
						break;
					case SLANG_STAGE_DOMAIN:
						outProgramData.shaderType = ECompileShaderType::eTessEvl;
						break;
					case SLANG_STAGE_GEOMETRY:
						outProgramData.shaderType = ECompileShaderType::eGeom;
						break;
					case SLANG_STAGE_FRAGMENT:
						outProgramData.shaderType = ECompileShaderType::eFrag;
						break;
					case SLANG_STAGE_COMPUTE:
						outProgramData.shaderType = ECompileShaderType::eComp;
						break;
					case SLANG_STAGE_RAY_GENERATION:
						outProgramData.shaderType = ECompileShaderType::eRaygen;
						break;
					case SLANG_STAGE_INTERSECTION:
						outProgramData.shaderType = ECompileShaderType::eIntersect;
						break;
					case SLANG_STAGE_ANY_HIT:
						outProgramData.shaderType = ECompileShaderType::eAnyhit;
						break;
					case SLANG_STAGE_CLOSEST_HIT:
						outProgramData.shaderType = ECompileShaderType::eClosehit;
						break;
					case SLANG_STAGE_MISS:
						outProgramData.shaderType = ECompileShaderType::eMiss;
						break;
					case SLANG_STAGE_CALLABLE:
						outProgramData.shaderType = ECompileShaderType::eCallable;
						break;
					case SLANG_STAGE_MESH:
						outProgramData.shaderType = ECompileShaderType::eMesh;
						break;
					case SLANG_STAGE_AMPLIFICATION:
						outProgramData.shaderType = ECompileShaderType::eMax;
						break;
					default:
						outProgramData.shaderType = ECompileShaderType::eMax;
						break;
					}
					


					outProgramData.entryPointName = entryPointRef->getName();
					outProgramData.data.resize(kernelBlob->getBufferSize());
					memcpy(outProgramData.data.data(), kernelBlob->getBufferPointer(), kernelBlob->getBufferSize());

					fprintf(stderr, "Debug: %s\n", (char*)outProgramData.data.data());

					
					outputTargetResult.programs.push_back(outProgramData);
				}

				ShaderReflectionData reflectionData = {};

				BindingInfo baseBias;
				int globalUniformGroupID = -1;
				{
					size_t globalBufferSize = layout->getGlobalConstantBufferSize();
					uint32_t globalBinding = layout->getGlobalConstantBufferBinding();
					if (globalBufferSize > 0)
					{
						auto& globalBindingSpace = reflectionData.EnsureBindingSpace(globalBinding);
						globalUniformGroupID = globalBindingSpace.InitUniformGroup(0, -1, "__Global", 0, globalBufferSize, globalBufferSize, 1);
					}
				}
				uint32_t paramCount = layout->getParameterCount();
				for (uint32_t paramID = 0; paramID < paramCount; ++paramID)
				{
					auto param = layout->getParameterByIndex(paramID);
					ReflectVariable(baseBias, reflectionData, globalUniformGroupID, 1, param);
				}
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

	CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(IShaderCompilerManager, ShaderCompilerManager);
}


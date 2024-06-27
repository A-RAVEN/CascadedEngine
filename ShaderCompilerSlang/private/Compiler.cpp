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
				PushTarget(SLANG_SPIRV, "glsl_450");
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
			//CompilerOptionEntry{ CompilerOptionName::EmitSpirvDirectly , CompilerOptionValue{ CompilerOptionValueKind::Int, 1 }} ,
			CompilerOptionEntry{ CompilerOptionName::DebugInformation , CompilerOptionValue{ CompilerOptionValueKind::Int, SLANG_DEBUG_INFO_LEVEL_STANDARD  }} ,
			CompilerOptionEntry{ CompilerOptionName::Optimization , CompilerOptionValue{ CompilerOptionValueKind::Int, SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_NONE }},
			CompilerOptionEntry{ CompilerOptionName::MatrixLayoutRow , CompilerOptionValue{ CompilerOptionValueKind::Int, 1 }},
			CompilerOptionEntry{ CompilerOptionName::MatrixLayoutColumn , CompilerOptionValue{ CompilerOptionValueKind::Int, 0 }},
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

		castl::vector<slang::ParameterCategory> UnwrapCategories(slang::VariableLayoutReflection* variable)
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


		void Reflect(ShaderReflectionData& reflectionData
			, slang::VariableLayoutReflection* variable
			, slang::TypeLayoutReflection* typeLayout
			, ParameterCategory variableCategory
			, BindingData const& bindingData
			, uint32_t parentArrayLength)
		{
			slang::TypeReflection::Kind kind = typeLayout->getKind();
			//处理混合类型
			if(variableCategory == ParameterCategory::Mixed)
			{
				unsigned categoryCount = variable->getCategoryCount();
				for (unsigned cc = 0; cc < categoryCount; cc++)
				{
					slang::ParameterCategory subCategory = variable->getCategoryByIndex(cc);
					Reflect(reflectionData, variable, typeLayout, subCategory, bindingData, parentArrayLength);
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
						Reflect(reflectionData, elementVarLayout, elementVarLayout->getTypeLayout(), elementCategory, newBinding, unrolledArrayLength);
					}
				}
			}
			if (kind == slang::TypeReflection::Kind::ParameterBlock)
			{
				auto elementVarLayout = typeLayout->getElementVarLayout();
				auto categories = UnwrapCategories(elementVarLayout);
				for (auto elementCategory : categories)
				{
					Reflect(reflectionData, elementVarLayout, elementVarLayout->getTypeLayout(), elementCategory, newBinding, unrolledArrayLength);
				}
			}
			else if (kind == slang::TypeReflection::Kind::Struct)
			{
				//第一次追踪Struct类型中的Uniform成员时，创建UniformGroup
				if (variableCategory == ParameterCategory::Uniform)
				{
					uint32_t strideInBytes = typeLayout->getStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
					uint32_t sizeInBytes = typeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
					newBinding.uniformGroupID = bindingSpace.InitUniformGroup(newBinding.bindingIndex
						, newBinding.uniformGroupID
						, newBinding.elementName, newBinding.memoryByteOffset, sizeInBytes, strideInBytes, elementCount);
					fprintf(stderr, "%s space: %d binding: %d arrayLength: %d category: %s\n", newBinding.path.c_str(), newBinding.bindingSpace, newBinding.bindingIndex, elementCount, GetCategoryName(variableCategory));
				}
				else
				{
					newBinding.resourceGroupID = bindingSpace.InitResourceGroup(newBinding.elementName, newBinding.resourceGroupID);
				}

				unsigned fieldCount = typeLayout->getFieldCount();
				for (uint32_t i = 0; i < fieldCount; i++)
				{
					slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
					auto categories = UnwrapCategories(field);
					for (auto fieldCategory : categories)
					{
						if (fieldCategory == variableCategory)
						{
							Reflect(reflectionData, field, field->getTypeLayout(), fieldCategory, newBinding, unrolledArrayLength);
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
				newElement.Init(newBinding.elementName, newBinding.memoryByteOffset, sizeInBytes, strideInBytes, elementCount);
				bindingSpace.AddElementToGroup(newBinding.bindingIndex, newBinding.uniformGroupID, newElement);
				fprintf(stderr, "%s space: %d binding: %d arrayLength: %d category: %s\n", newBinding.path.c_str(), newBinding.bindingSpace, newBinding.bindingIndex, elementCount, GetCategoryName(variableCategory));
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
				fprintf(stderr, "%s bindingType: %s space: %d binding: %d arrayLength: %d category: %s\n", newBinding.path.c_str(), GetBindingTypeName(bindingType), newBinding.bindingSpace, newBinding.bindingIndex, unrolledArrayLength, GetCategoryName(variableCategory));
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
				ShaderReflectionData reflectionData = {};

				int entryPointCount = layout->getEntryPointCount();
				outputTargetResult.programs.reserve(entryPointCount);
				EShaderTypeFlags shaderTypeFlags = 0;
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

					//fprintf(stderr, "Debug: %s\n", (char*)outProgramData.data.data());
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
				{
					size_t globalBufferSize = layout->getGlobalConstantBufferSize();
					uint32_t globalBinding = layout->getGlobalConstantBufferBinding();
					auto& globalBindingSpace = reflectionData.EnsureBindingSpace(globalBinding);
					if (globalBufferSize > 0)
					{
						bindingData.uniformGroupID = globalBindingSpace.InitUniformGroup(0, -1, "__Global", 0, globalBufferSize, globalBufferSize, 1);
					}
					//bindingData.resourceGroupID = globalBindingSpace.InitResourceGroup("__Global", bindingData.resourceGroupID);
				}
				uint32_t paramCount = layout->getParameterCount();
				for (uint32_t paramID = 0; paramID < paramCount; ++paramID)
				{
					auto param = layout->getParameterByIndex(paramID);
					Reflect(reflectionData, param, param->getTypeLayout(), param->getCategory(), bindingData, 1);
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


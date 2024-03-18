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
			CompilerOptionEntry{ CompilerOptionName::VulkanUseEntryPointName , CompilerOptionValue{ CompilerOptionValueKind::Int, 1 }}
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

		void ReflectSlangVariable(SimpleBindingOffset parentOffset, Name parentName,
			slang::VariableLayoutReflection* variable)
		{
			slang::TypeReflection* type = variable->getType();
			slang::TypeLayoutReflection* typeLayout = variable->getTypeLayout();
			slang::TypeReflection::Kind kind = variable->getTypeLayout()->getKind();
			SlangResourceAccess resourceAccess = variable->getTypeLayout()->getResourceAccess();

			int descBindingOffset = variable->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM);
			int descSetOffset = variable->getOffset(SLANG_PARAMETER_CATEGORY_REGISTER_SPACE);
			SimpleBindingOffset offset(variable);
			offset += parentOffset;

			if (kind == slang::TypeReflection::Kind::ConstantBuffer)
			{
				slang::BindingType bindingType = typeLayout->getBindingRangeType(0);

				if (bindingType == slang::BindingType::PushConstant)
				{
					SPIRVShaderVariableReflectionDescription desc;
					{
						desc.name = ConcatenateName(parentName, Name(variable->getName()));
						desc.type = ShaderResourceType::PUSH_CONSTANT;
						desc.descriptorSet = offset.bindingSet + offset.childSet;
						desc.binding = offset.binding;
						desc.memoryAccess = ShaderResourceMemoryAccess::READ;
						desc.arraySize = 1;
					}

					RefCounted<IShaderVariableReflection> variableReflection =
						MAKE_RC_OBJECT(SPIRVShaderVariableReflection, m_slangSPIRVShaderVariableReflectionPool, desc);

					m_shaderVariables.push_back(variableReflection);
					return;
				}

				SPIRVShaderVariableReflectionDescription desc;
				{
					desc.name = ConcatenateName(parentName, Name(variable->getName()));
					desc.type = ShaderResourceType::UNIFORM_BUFFER;
					desc.descriptorSet = offset.bindingSet + offset.childSet;
					desc.binding = offset.binding;
					desc.memoryAccess = GetShaderMemoryAccess(resourceAccess);
					desc.arraySize = 1;
				}

				//AddOrModifySPIRVResource(desc);
			}
			else if (kind == slang::TypeReflection::Kind::ParameterBlock)
			{
				typeLayout = UnwrapParameterGroups(typeLayout);

				for (Uint32 i = 0; i < typeLayout->getFieldCount(); i++)
				{
					slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
					ReflectSlangVariable(offset, Name(variable->getName()), field);
				}
			}
			else if (kind == slang::TypeReflection::Kind::Scalar || kind == slang::TypeReflection::Kind::Vector || kind ==
				slang::TypeReflection::Kind::Matrix)
			{
				SPIRVShaderVariableReflectionDescription desc;
				{
					desc.name = ConcatenateName(parentName, Name(variable->getName()));
					desc.descriptorSet = offset.bindingSet + offset.childSet;
					desc.binding = offset.binding;
					desc.type = ShaderResourceType::UNIFORM_BUFFER;
					desc.memoryAccess = GetShaderMemoryAccess(resourceAccess);
					desc.arraySize = 1;
				}

				AddOrModifySPIRVResource(desc);
			}
			else if (kind == slang::TypeReflection::Kind::Struct)
			{
				auto temp = typeLayout->getFieldCount();
				auto name = variable->getName();

				if (IsStructOnlyComposedWithOrdinaryData(typeLayout))
				{
					SPIRVShaderVariableReflectionDescription desc;
					{
						desc.name = ConcatenateName(parentName, Name(variable->getName()));
						desc.descriptorSet = offset.bindingSet + offset.childSet;
						desc.binding = offset.binding;
						desc.type = ShaderResourceType::UNIFORM_BUFFER;
						desc.memoryAccess = GetShaderMemoryAccess(resourceAccess);
						desc.arraySize = 1;
					}

					AddOrModifySPIRVResource(desc);
				}
				else
				{
					for (Uint32 i = 0; i < typeLayout->getFieldCount(); i++)
					{
						slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
						ReflectSlangVariable(offset, Name(name), field);
					}
				}
			}
			else if (kind == slang::TypeReflection::Kind::Resource ||
				kind == slang::TypeReflection::Kind::SamplerState ||
				kind == slang::TypeReflection::Kind::TextureBuffer ||
				kind == slang::TypeReflection::Kind::ShaderStorageBuffer)
			{
				LUMENGINE_ASSERT(typeLayout->getBindingRangeCount() == 1); // should be true for a leaf resource parameter
				slang::BindingType bindingType = typeLayout->getBindingRangeType(0);

				SPIRVShaderVariableReflectionDescription desc;
				{
					desc.name = ConcatenateName(parentName, Name(variable->getName()));
					desc.descriptorSet = offset.bindingSet + offset.childSet;
					desc.binding = offset.binding;
					desc.type = ConvertSlangType(bindingType);
					desc.memoryAccess = GetShaderMemoryAccess(resourceAccess);
					desc.arraySize = 1;
				}

				AddOrModifySPIRVResource(desc);
			}
			else if (kind == slang::TypeReflection::Kind::Array)
			{
				slang::BindingType bindingType = typeLayout->getBindingRangeType(0);

				SPIRVShaderVariableReflectionDescription desc;
				{
					desc.name = ConcatenateName(parentName, Name(variable->getName()));
					desc.descriptorSet = offset.bindingSet + offset.childSet;
					desc.binding = offset.binding;
					desc.type = ConvertSlangType(bindingType);
					desc.memoryAccess = GetShaderMemoryAccess(resourceAccess);
					desc.arraySize = static_cast<Uint32>(typeLayout->getElementCount());
				}

				AddOrModifySPIRVResource(desc);
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


					uint32_t paramCount = layout->getParameterCount();
					for (uint32_t paramID = 0; paramID < paramCount; ++paramID)
					{
						auto param = layout->getParameterByIndex(paramID);
						auto paramCategory = param->getCategory();
						slang::TypeReflection* paramType = param->getType();
						auto paramName = param->getName();
						fprintf(stderr, "field type %s: %s\n", paramName, paramType->getName());
						//param->getPendingDataLayout
						if (paramType->getKind() == slang::TypeReflection::Kind::ConstantBuffer
							|| paramType->getKind() == slang::TypeReflection::Kind::Struct
							|| paramType->getKind() == slang::TypeReflection::Kind::ParameterBlock
							)
						{
							int fieldCount = paramType->getFieldCount();
							fprintf(stderr, "ConstantBuffer %s: field count %d\n", paramName, fieldCount);
							for (int fieldID = 0; fieldID < fieldCount; ++fieldID)
							{
								auto field = paramType->getFieldByIndex(fieldID);
								auto fieldName = field->getName();
								auto fieldType = field->getType();
								fprintf(stderr, "Field %s: type %s\n", fieldName, fieldType->getName());
							}
						}
					}
					outputTargetResult.programs.push_back(outProgramData);
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


#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
#define NV_EXTENSIONS
#include <header/Compiler.h>
#include <slang.h>
#include <map>
#include <set>
#include <string>
#include <mutex>
#include <CACore/header/LibraryExportCommon.h>
#include <CACore/header/DebugUtils.h>

namespace ShaderCompilerSlang
{

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
			m_Session = spCreateSession(NULL);
		}

		void Release()
		{
			spDestroySession(m_Session);
		}

		virtual void AddInlcudePath(const char* path) override
		{
			spAddSearchPath(m_CompileTask.m_Request, path);
		}

		virtual void SetTarget(EShaderTargetType targetType) override
		{
			switch (targetType)
			{
			case ShaderCompilerSlang::EShaderTargetType::eSpirV:
				spSetCodeGenTarget(m_CompileTask.m_Request, SLANG_SPIRV);
				break;
			default:
				break;
			}
		}

		virtual void SetMacro(const char* macro_name, const char* macro_value) override
		{
			spAddPreprocessorDefine(m_CompileTask.m_Request, macro_name, macro_value);
		}


		virtual void BeginCompileTask() override
		{
			m_CompileTask.m_Request = spCreateCompileRequest(m_Session);
			m_CompileTask.m_TranslationUnitIndex = spAddTranslationUnit(m_CompileTask.m_Request, SLANG_SOURCE_LANGUAGE_SLANG, "");
		}

		virtual void EndCompileTask() override
		{
			spDestroyCompileRequest(m_CompileTask.m_Request);
			m_CompileTask.Clear();
		}

		virtual void AddSourceFile(const char* path) override
		{
			std::string path_str = path;
			if (m_CompileTask.m_SourceFiles.find(path_str) == m_CompileTask.m_SourceFiles.end())
			{
				m_CompileTask.m_SourceFiles.insert(path_str);
				spAddTranslationUnitSourceFile(m_CompileTask.m_Request, m_CompileTask.m_TranslationUnitIndex, path);
			}
		}

		virtual int AddEntryPoint(const char* name, ECompileShaderType shader_type) override
		{
			std::string name_str = name;
			auto found = m_CompileTask.m_EntryPointToID.find(name_str);
			if (found == m_CompileTask.m_EntryPointToID.end())
			{
				int id = spAddEntryPoint(m_CompileTask.m_Request, m_CompileTask.m_TranslationUnitIndex, name, SHADER_KIND_TABLE[static_cast<uint32_t>(shader_type)]);
				found = m_CompileTask.m_EntryPointToID.insert(std::make_pair(name_str, id)).first;
			}
			return found->second;
		};

		virtual void Compile() override
		{
			int anyErrors = spCompile(m_CompileTask.m_Request);
			m_CompileTask.m_ErrorsOrWarnings = spGetDiagnosticOutput(m_CompileTask.m_Request);
			m_CompileTask.m_HasError = anyErrors != 0;
			if (!m_CompileTask.m_HasError)
			{
				m_CompileTask.m_OutputData.resize(m_CompileTask.m_EntryPointToID.size());
				for (auto pair : m_CompileTask.m_EntryPointToID)
				{
					size_t dataSize = 0;
					void const* data = spGetEntryPointCode(m_CompileTask.m_Request, pair.second, &dataSize);
					m_CompileTask.m_OutputData[pair.second].resize(dataSize);
					memcpy(m_CompileTask.m_OutputData[pair.second].data(), data, dataSize);
				}
			}
			if (m_CompileTask.m_HasError)
			{
				CA_LOG_ERR(m_CompileTask.m_ErrorsOrWarnings);
			}
		}

		virtual bool HasError() const override
		{
			return m_CompileTask.m_HasError;
		}
		
	private:
		SlangSession* m_Session = nullptr;

		struct CompileTask
		{
			SlangCompileRequest* m_Request = nullptr;
			int m_TranslationUnitIndex = -1;
			std::map<std::string, int> m_EntryPointToID;
			std::set<std::string> m_SourceFiles;
			std::vector<std::vector<uint8_t>> m_OutputData;
			std::string m_ErrorsOrWarnings;
			bool m_HasError = false;

			void Clear()
			{
				m_Request = nullptr;
				m_TranslationUnitIndex = -1;
				m_EntryPointToID.clear();
				m_SourceFiles.clear();
				m_OutputData.clear();
				m_ErrorsOrWarnings.clear();
				m_HasError = false;
			}
		} m_CompileTask;
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
			std::unique_lock<std::mutex> lock(m_Mutex);
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
				std::lock_guard<std::mutex> lock(m_Mutex);
				m_AvailableCompilers.push_back(compiler);
			}
			m_ConditinalVariable.notify_one();
		}

		virtual void InitializePoolSize(uint32_t compiler_count) override
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
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

		std::condition_variable m_ConditinalVariable;
		std::mutex m_Mutex;
		std::vector<IShaderCompiler*> m_AvailableCompilers;
		std::vector<Compiler_Impl> m_Compilers;
	};

	CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(IShaderCompilerManager, ShaderCompilerManager);
}


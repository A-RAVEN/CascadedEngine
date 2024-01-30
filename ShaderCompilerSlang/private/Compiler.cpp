#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
#define NV_EXTENSIONS
//#include <header/Compiler.h>
#include <slang.h>

namespace ShaderCompiler
{
	static shaderc_shader_kind SHADER_KIND_TABLE[] =
	{
		shaderc_glsl_default_vertex_shader,
		shaderc_glsl_default_tess_control_shader,
		shaderc_glsl_default_tess_evaluation_shader,
		shaderc_glsl_default_geometry_shader,
		shaderc_glsl_default_fragment_shader,
		shaderc_glsl_default_compute_shader,

		//nvidia mesh shader
		shaderc_task_shader,
		shaderc_mesh_shader,

		//nvidia raytracing shader
		shaderc_raygen_shader,
		shaderc_anyhit_shader,
		shaderc_closesthit_shader,
		shaderc_miss_shader,
		shaderc_intersection_shader,
		shaderc_callable_shader,
	};

	static shaderc_source_language SOURCE_LANGUAGE_TABLE[] =
	{
		shaderc_source_language_hlsl,
		shaderc_source_language_glsl
	};

	class Compiler_Impl// : public IShaderCompiler
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
			m_Request = spCreateCompileRequest(session);
		}

		void Release()
		{
			spDestroySession(m_Session);
			spDestroyCompileRequest(m_Request);
		}

		virtual void AddInlcudePath(const char* path) override
		{
			spAddSearchPath(m_Request, path);
		}
		virtual void SetMacro(const char* macro_name, const char* macro_value) override
		{
			spAddPreprocessorDefine(m_Request, macro_name, macro_value);
		}
		virtual void SetTarget()
		{
			spSetCodeGenTarget(m_Request, SLANG_SPIRV);

		}

		void AddSourceFile()
		{
			spAddTranslationUnit(m_Request, SLANG_SOURCE_LANGUAGE_SLANG, "");
			
		}
		
	private:
		SlangSession* m_Session = nullptr;
		SlangCompileRequest* m_Request = nullptr;
	};

	CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(IShaderCompiler, Compiler_Impl)
}


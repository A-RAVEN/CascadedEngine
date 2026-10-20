// CSharpHost：把 CoreCLR 拉进本进程的宿主模块（手写壳）。
//
// 它只做四件事：定位 hostfxr → 拉起运行时 → 找到托管侧 Boot / Tick 的入口 → 每帧调一次 Tick。
// 不链接 CoreCLR（hostfxr 是运行时 LoadLibrary 出来的），所以换 .NET 版本不用重编本模块。
// 任何一步失败都必须把"找过哪些路径 / 版本号"写进 LastError，由调用方打日志 —— 不许静默降级。
#include <CACore/CAModuleManager.h>
#include <ScriptHost/IScriptHost.h>
// 生成的绑定产物：提供 IWindow_GetApi 与 IWindowApi_v1（本 TU 编译它，模块自己就带表）
#include "IWindow.Bindings.cpp"

#define CA_IMPLEMENT_MODULE 1
#include <CACore/CAModuleImplementation.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef CASCADED_HAS_NETHOST
#include <nethost.h>
#include <coreclr_delegates.h>
#include <hostfxr.h>
#endif

namespace cs_script
{
	// 交给托管侧的函数表。字段顺序与宽度就是 ABI，只许在末尾追加；
	// 托管侧的 EngineApi（Scripting/Managed/Host.cs）必须逐字相同。
	struct EngineApi
	{
		uint32_t abi_version;
		uint32_t size;
		void (*log)(int level, uint8_t const* utf8, uint64_t len);
		int32_t (*echo)(int32_t value);
		// --- v2 追加（csharp-bindings-generator 任务 3.2）---
		// 取"接口函数表 + 实例指针"：id 见 CSharpHost/README.md 的编号表（1 = IWindow）；
		// version/size 是托管侧期望的值，成功后 out 填表、*instanceOut 填实例，返回真实表大小；
		// 版本或大小不符时返回 0 且**一个字节都不写**（与生成的 GetApi 同一口径）。
		uint32_t (*get_interface_api)(uint32_t interfaceId, uint32_t version, uint32_t size, void* out,
									  void** instanceOut);
	};

	// 引擎侧约定的接口编号（托管侧 Host.cs 必须用同一组数字）
	static constexpr uint32_t s_interfaceIdWindow = 1;


	static constexpr uint32_t s_engineApiVersion = 2;

	class CSharpHost final : public IScriptHost
	{
	public:
		CSharpHost() = default;
		// 关闭放在析构里：实例析构发生在模块管理器拆实例的时候，此时 CSharpHost.dll 还没被卸载；
		// 实测把 hostfxr_close 放到更晚（模块的 TryRelease / 进程退出之后）会在 CSharpHost.DLL 里崩
		// （0xC0000005，dump 见 change 的 evidence）。
		~CSharpHost() override
		{
			Shutdown();
			if (s_instance == this)
			{
				s_instance = nullptr;
			}
		}

		// 把接口实例登记进来（任务 3.2 的"登记一行"）：编号 1 = IWindow
		void SetInterfaceInstance(uint32_t interfaceId, void* instance) override
		{
			if (interfaceId < 8u)
			{
				s_interfaceInstances[interfaceId] = instance;
			}
		}
		// 模块 link 阶段（所有模块都登记完之后）被调用一次
		void Init(cacore::IModuleManager* pModuleManager)
		{
			(void)pModuleManager;
			s_instance = this;
			LoadRuntime();
		}

		bool IsAvailable() const override { return m_bootSucceeded; }

		int32_t Tick(double deltaSeconds) override
		{
			if (!m_bootSucceeded || m_tick == nullptr)
			{
				return -1;
			}

			m_expectedTick++;
			int32_t value = m_tick(deltaSeconds);
			if (value != m_expectedTick)
			{
				CA_LOG_ERR("托管侧 Tick 回值不符：期望 {}，实际 {}", m_expectedTick, value);
			}
			else
			{
				CA_LOG("托管侧 Tick #{} 回值 {}（引擎侧计数与之一致）", m_expectedTick, value);
			}
			return value;
		}

		char const* LastError() const override { return m_lastError.c_str(); }

		void Shutdown() override
		{
#ifdef CASCADED_HAS_NETHOST
			// 关闭阶段托管侧还可能回调过来（实测 hostfxr_close 期间 coreclr 会再调一次 log），
			// 那时进程里的日志设施已经/正在销毁，再走 CA_LOG 会崩在 CA_LOG 里。
			// 所以先把开关立起来，让回调改走 stderr。
			s_shuttingDown = true;
			if (m_runtimeContext != nullptr && m_closeFn != nullptr)
			{
				m_closeFn(m_runtimeContext);
				m_runtimeContext = nullptr;
				CA_LOG("CSharpHost：运行时已关闭（hostfxr_close 已调用）");
				fprintf(stderr, "[CSharpHost] 运行时已关闭（hostfxr_close 已调用）\n");
				fflush(stderr);
			}
			if (m_hostfxrLibrary != nullptr)
			{
				// 故意不 FreeLibrary：实测在运行时仍会用到 hostfxr 的时候卸载它，进程当场 0xC0000005
				// （崩在 CSharpHost.DLL 里、紧跟在"已卸载"那一行之后）。hostfxr 的生命周期就是进程的
				// 生命周期，退出时由系统回收 —— 这是宿主程序的通行做法，不是泄漏。
				fprintf(stderr, "[CSharpHost] hostfxr 保持加载（不 FreeLibrary：卸载会让仍在跑的运行时代码崩）\n");
				fflush(stderr);
			}
#endif
			m_bootSucceeded = false;
			m_tick = nullptr;
		}

	private:
		// 注意：这张表必须活到进程结束 —— 托管侧只保存指针，不拷贝内容。
		// 第一版把它写成 LoadRuntime() 里的局部变量，Boot 之后表就失效了，Tick 里一读就是野指针（实测崩在
		// Scripting.Managed.Host.Tick，退出码 0xC0000005）。改成成员。
		EngineApi m_api{};
		// 被登记的接口实例（下标就是接口编号；0 号不用）
		void* m_interfaceInstances[8] = {};

		void LoadRuntime()
		{
			auto started = std::chrono::steady_clock::now();
			m_foundPaths.clear();

#ifdef CASCADED_HAS_NETHOST
			if (!LoadHostfxr())
			{
				Fatal(m_lastError);
			}

			std::wstring const configPath = RuntimeConfigPath();
			m_foundPaths.push_back(ToUtf8(configPath));
			if (!std::filesystem::exists(configPath))
			{
				Fatal("找不到托管侧的 runtimeconfig.json：" + ToUtf8(configPath));
			}

			if (!InitializeRuntime(configPath))
			{
				Fatal(m_lastError);
			}

			if (!ResolveEntries())
			{
				Fatal(m_lastError);
			}

			EngineApi& api = m_api;
			api.abi_version = s_engineApiVersion;
#if defined(_DEBUG)
			// 自检开关（只在 Debug 生效）：让层 ③ 的失败路径测试不必临时改源码就能构造"ABI 不符"。
			// 用法见 CSharpHost/README.md 的环境变量表。
			if (SelfTestMode() == "abi-mismatch")
			{
				api.abi_version = s_engineApiVersion + 1;
				CA_LOG_ERR("CSharpHost 自检开关已生效：故意把 abi_version 改成 {}（真实值 {}）",
					api.abi_version, s_engineApiVersion);
			}
#endif
			api.size = static_cast<uint32_t>(sizeof(EngineApi));
			api.log = &CSharpHost::OnManagedLog;
			api.echo = &CSharpHost::OnManagedEcho;
			api.get_interface_api = &CSharpHost::OnGetInterfaceApi;
			CA_LOG("CSharpHost：引擎侧 OnGetInterfaceApi 的地址 = {:X}，函数表 {} 字节",
				reinterpret_cast<uintptr_t>(&CSharpHost::OnGetInterfaceApi), api.size);

			int32_t bootResult = m_boot(&api);
			if (bootResult != 0)
			{
				Fatal("托管侧 Boot 返回错误码 " + std::to_string(bootResult) +
					"（引擎侧 ABI 版本 " + std::to_string(api.abi_version) +
					"、函数表 " + std::to_string(api.size) + " 字节；0 = 成功）");
			}

			m_bootSucceeded = true;
			auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started).count();
			CA_LOG("CSharpHost：托管运行时已就绪（加载耗时 = {} ms；函数表 {} 字节，ABI v{}）",
				static_cast<long long>(elapsedMs), api.size, api.abi_version);
#else
			Fail("本机构建时没找到 nethost，CSharpHost 编不出真实的加载路径");
			CA_LOG_ERR("CSharpHost：{}", m_lastError.c_str());
#endif
		}

#ifdef CASCADED_HAS_NETHOST
		bool LoadHostfxr()
		{
			char_t buffer[1024];
			size_t size = sizeof(buffer) / sizeof(char_t);
			int rc = get_hostfxr_path(buffer, &size, nullptr);
			if (rc != 0)
			{
				std::string tried = "nethost 的 get_hostfxr_path 返回 " + std::to_string(rc);
				if (!m_envConfigPath.empty())
				{
					tried += "；另外试过 CASCADED_CSHARP_RUNTIMECONFIG=" + m_envConfigPath;
				}
				Fail("找不到 hostfxr（" + tried + "）");
				return false;
			}

			m_foundPaths.push_back(ToUtf8(buffer));
			m_hostfxrLibrary = ::LoadLibraryW(buffer);
			if (m_hostfxrLibrary == nullptr)
			{
				Fail("hostfxr 找到了但 LoadLibrary 失败：" + ToUtf8(buffer));
				return false;
			}

			m_initFn = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
				::GetProcAddress(m_hostfxrLibrary, "hostfxr_initialize_for_runtime_config"));
			m_getDelegateFn = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
				::GetProcAddress(m_hostfxrLibrary, "hostfxr_get_runtime_delegate"));
			m_closeFn = reinterpret_cast<hostfxr_close_fn>(
				::GetProcAddress(m_hostfxrLibrary, "hostfxr_close"));
			if (m_initFn == nullptr || m_getDelegateFn == nullptr || m_closeFn == nullptr)
			{
				Fail("hostfxr 缺少必需导出（initialize_for_runtime_config / get_runtime_delegate / close 至少缺一个）");
				return false;
			}
			return true;
		}

		bool InitializeRuntime(std::wstring const& configPath)
		{
			hostfxr_handle context = nullptr;
			int rc = m_initFn(configPath.c_str(), nullptr, &context);
			if (rc != 0 || context == nullptr)
			{
				Fail("hostfxr_initialize_for_runtime_config 返回 " + std::to_string(rc) +
					"（配置 " + ToUtf8(configPath) + "；配置里要的框架版本：" + RequestedFramework(configPath) +
					"；本机已装的运行时版本：" + SharedRuntimeVersions() + "）");
				if (context != nullptr)
				{
					m_closeFn(context);
				}
				return false;
			}

			void* loadAssemblyAndGetFunctionPointer = nullptr;
			rc = m_getDelegateFn(context, hdt_load_assembly_and_get_function_pointer,
				&loadAssemblyAndGetFunctionPointer);
			if (rc != 0 || loadAssemblyAndGetFunctionPointer == nullptr)
			{
				Fail("取 load_assembly_and_get_function_pointer 委托失败，返回 " + std::to_string(rc));
				m_closeFn(context);
				return false;
			}

			m_loadAssembly = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(
				loadAssemblyAndGetFunctionPointer);
			m_runtimeContext = context; // 故意不关：整个进程生命周期内保留运行时上下文
			return true;
		}

		bool ResolveEntries()
		{
			std::wstring const assemblyPath = AssemblyPath();
			std::wstring const typeName = L"Scripting.Managed.Host, Scripting.Managed";

			int rc = m_loadAssembly(assemblyPath.c_str(), typeName.c_str(), L"Boot",
				UNMANAGEDCALLERSONLY_METHOD, nullptr,
				reinterpret_cast<void**>(&m_boot));
			if (rc != 0 || m_boot == nullptr)
			{
				Fail("解析托管入口 Boot 失败，返回 " + std::to_string(rc) +
					"（程序集 " + ToUtf8(assemblyPath) + "）");
				return false;
			}

			rc = m_loadAssembly(assemblyPath.c_str(), typeName.c_str(), L"Tick",
				UNMANAGEDCALLERSONLY_METHOD, nullptr,
				reinterpret_cast<void**>(&m_tick));
			if (rc != 0 || m_tick == nullptr)
			{
				Fail("解析托管入口 Tick 失败，返回 " + std::to_string(rc));
				return false;
			}
			return true;
		}

		// 读自检开关（CASCADED_CSHARP_SELFTEST）：只在 Debug 生效，取值见 README。
		static std::string SelfTestMode()
		{
			char* value = nullptr;
			size_t length = 0;
			if (_dupenv_s(&value, &length, "CASCADED_CSHARP_SELFTEST") == 0 && value != nullptr)
			{
				std::string result(value);
				free(value);
				return result;
			}
			free(value);
			return std::string();
		}

		static std::wstring RuntimeConfigPath()
		{
			char* envValue = nullptr;
			size_t envLength = 0;
			if (_dupenv_s(&envValue, &envLength, "CASCADED_CSHARP_RUNTIMECONFIG") == 0 && envValue != nullptr &&
				envValue[0] != '\0')
			{
				std::wstring result = Utf8ToWide(envValue);
				free(envValue);
				return result;
			}
			free(envValue);
			return Utf8ToWide(ExeDirectory() + "\\Scripting.Managed.runtimeconfig.json");
		}

		static std::wstring AssemblyPath()
		{
			return Utf8ToWide(ExeDirectory() + "\\Scripting.Managed.dll");
		}

		static std::string ExeDirectory()
		{
			char buffer[MAX_PATH] = {};
			DWORD written = ::GetModuleFileNameA(nullptr, buffer, MAX_PATH);
			std::string path(buffer, written);
			size_t slash = path.find_last_of("\\/");
			return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
		}

		static std::wstring Utf8ToWide(std::string const& text)
		{
			if (text.empty())
			{
				return std::wstring();
			}
			int needed = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
			std::wstring wide((size_t)needed, L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide.data(), needed);
			return wide;
		}

		static std::string ToUtf8(std::wstring const& wide)
		{
			if (wide.empty())
			{
				return std::string();
			}
			int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
			std::string narrow((size_t)needed, '\0');
			::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), narrow.data(), needed, nullptr, nullptr);
			return narrow;
		}
#endif

		void Fail(std::string const& reason)
		{
			m_lastError = reason;
			// 调用方可能把已经带过路径的 m_lastError 再传进来（Fatal(m_lastError)）——别把路径拼两遍
			if (m_lastError.find("找过的路径") != std::string::npos)
			{
				return;
			}
			if (!m_foundPaths.empty())
			{
				m_lastError += "；找过的路径：";
				for (size_t index = 0; index < m_foundPaths.size(); index++)
				{
					if (index != 0)
					{
						m_lastError += "、";
					}
					m_lastError += m_foundPaths[index];
				}
			}
		}

		// 失败即"初始化失败"：引擎里没有错误回传通道，所以按 spec 的硬判据让进程以非零码退出，
		// 而不是带着"没有 C# 支持"继续跑（那正是 spec 里 MUST NOT 的静默降级）。
		[[noreturn]] void Fatal(std::string const& reason)
		{
			Fail(reason);
			CA_LOG_ERR("CSharpHost 初始化失败（退出码 3）：{}", m_lastError.c_str());
			std::exit(3);
		}

		// 版本不匹配时也要把"配置里要的是哪个版本"带进日志（spec 要求错误信息里有它）
		static std::string RequestedFramework(std::wstring const& configPath)
		{
			std::ifstream stream(configPath);
			if (!stream.is_open())
			{
				return "(读不到配置内容)";
			}

			std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			size_t versionAt = content.find("\"version\"");
			if (versionAt == std::string::npos)
			{
				return "(配置里没有 version 字段)";
			}
			size_t colon = content.find(':', versionAt);
			if (colon == std::string::npos)
			{
				return "(配置里的 version 字段读不出来)";
			}
			size_t quoteBegin = content.find('"', colon);
			size_t quoteEnd = quoteBegin == std::string::npos ? std::string::npos : content.find('"', quoteBegin + 1);
			if (quoteBegin == std::string::npos || quoteEnd == std::string::npos)
			{
				return "(配置里的 version 字段读不出来)";
			}
			return content.substr(quoteBegin + 1, quoteEnd - quoteBegin - 1);
		}

		// 版本不匹配时要列出本机实际有哪些运行时版本（spec 的第二条失败判据要求可核信息）
		static std::string SharedRuntimeVersions()
		{
			std::string result;
			char* programFiles = nullptr;
			size_t length = 0;
			if (_dupenv_s(&programFiles, &length, "ProgramFiles") != 0 || programFiles == nullptr)
			{
				return "(读不到 ProgramFiles 环境变量)";
			}

			std::string pattern = std::string(programFiles) + "\\dotnet\\shared\\Microsoft.NETCore.App\\*";
			free(programFiles);

			WIN32_FIND_DATAA data{};
			HANDLE handle = ::FindFirstFileA(pattern.c_str(), &data);
			if (handle == INVALID_HANDLE_VALUE)
			{
				return "(在 " + pattern + " 下没找到任何运行时目录)";
			}

			do
			{
				if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
					std::string(data.cFileName) != "." && std::string(data.cFileName) != "..")
				{
					if (!result.empty())
					{
						result += "、";
					}
					result += data.cFileName;
				}
			} while (::FindNextFileA(handle, &data) != 0);
			::FindClose(handle);
			return result.empty() ? "(没有已装的运行时版本)" : result;
		}

		static void OnManagedLog(int level, uint8_t const* utf8, uint64_t len)
		{
			std::string text(reinterpret_cast<char const*>(utf8), (size_t)len);
			if (s_shuttingDown)
			{
				fprintf(stderr, "[托管侧·关闭期] %s\n", text.c_str());
				fflush(stderr);
				return;
			}
			if (level >= 2)
			{
				CA_LOG_ERR("[托管侧] {}", text.c_str());
			}
			else
			{
				CA_LOG("[托管侧] {}", text.c_str());
			}
		}

		static int32_t OnManagedEcho(int32_t value)
		{
			s_echoCalls++;
			return value + 1;
		}

		// 托管侧取"接口函数表 + 实例"的入口（csharp-bindings-generator 任务 3.2）。
		// 版本/大小不符、或实例没登记，就返回 0 且**一个字节都不写** —— 与生成出来的 GetApi 同一口径。
		static uint32_t OnGetInterfaceApi(uint32_t interfaceId, uint32_t version, uint32_t size, void* out,
										  void** instanceOut)
		{
			if (s_instance == nullptr || interfaceId != s_interfaceIdWindow || interfaceId >= 8u)
			{
				return 0u;
			}
			// `size == 0` 是"探针调用"：托管侧还不知道表有多大，先问一句（不写任何内存）。
			// 这一条必须排在 `out == nullptr` 之前 —— 探针调用本来就不给缓冲区（实测漏了这层判断，
			// 探针永远返回 0）。
			if (size == 0u)
			{
				return static_cast<uint32_t>(sizeof(IWindowApi_v1));
			}
			void* instance = s_interfaceInstances[interfaceId];
			if (instance == nullptr || out == nullptr)
			{
				return 0u;
			}
			const uint32_t realSize = IWindow_GetApi(version, size, static_cast<IWindowApi_v1*>(out));
			if (realSize == 0u)
			{
				return 0u;
			}
			if (instanceOut != nullptr)
			{
				*instanceOut = instance;
			}
			return realSize;
		}

		using BootFn = int32_t (*)(EngineApi*);
		using TickFn = int32_t (*)(double);

		bool m_bootSucceeded = false;
		BootFn m_boot = nullptr;
		TickFn m_tick = nullptr;
		int32_t m_expectedTick = 0;
		std::string m_lastError;
		std::vector<std::string> m_foundPaths;
		std::string m_envConfigPath;

#ifdef CASCADED_HAS_NETHOST
		HMODULE m_hostfxrLibrary = nullptr;
		hostfxr_handle m_runtimeContext = nullptr;
		hostfxr_initialize_for_runtime_config_fn m_initFn = nullptr;
		hostfxr_get_runtime_delegate_fn m_getDelegateFn = nullptr;
		hostfxr_close_fn m_closeFn = nullptr;
		load_assembly_and_get_function_pointer_fn m_loadAssembly = nullptr;
#endif

		static int32_t s_echoCalls;
		static bool s_shuttingDown;

	public:
		// TryRelease 被调用时，管理器里的实例可能已经先被释放了（实测 GetInstance 返回空），
		// 所以模块自己留一份指针 —— 它由本模块 new、也由本模块管，指针生命周期就是模块生命周期。
		static CSharpHost* s_instance;

		// 被登记的接口实例（下标就是接口编号；0 号不用）。用 **static**：登记方与读取方可能不是同一个
		// 实例（实测 tester 拿到的 IScriptHost* 与 s_instance 不是同一个 ⇒ 成员数组读不到）。
		static void* s_interfaceInstances[8];
	};

	int32_t CSharpHost::s_echoCalls = 0;
	bool CSharpHost::s_shuttingDown = false;
	CSharpHost* CSharpHost::s_instance = nullptr;
void* CSharpHost::s_interfaceInstances[8] = {};
}

// 框架对三个导出都是可选查找（CaModuleManager.cpp 里取地址后判空才调），而 CA_MODULE_INSTANCE 宏
// 只生成 TryInit；TryRelease 这一段在宏里是注释掉的 —— 所以要手写，才能在关闭时关掉运行时。
extern "C" CA_MODULE_API void TryRelease(void* pManager)
{
	// 关闭阶段每条测试的 stdout 已经被 freopen 到各自的日志、且可能已关闭，所以这里写 stderr：
	// stderr 没有被重定向，这行能证明"导出真的被调到"，与日志设施是否还活着无关。
	fprintf(stderr, "[CSharpHost] TryRelease 进入（写 stderr）\n");
	fflush(stderr);

	auto* manager = static_cast<cacore::IModuleManager*>(pManager);
	if (manager == nullptr)
	{
		return;
	}
	if (auto* host = cs_script::CSharpHost::s_instance)
	{
		CA_LOG("CSharpHost：TryRelease 开始（关闭托管运行时；若析构已经关过，这里是幂等空转）");
		host->Shutdown();
		cs_script::CSharpHost::s_instance = nullptr;
	}
}

CA_MODULE_INSTANCE(cs_script::IScriptHost, cs_script::CSharpHost, CSharpHost);

// CSharpHost：把 CoreCLR 拉进本进程的宿主模块（手写壳）。
//
// 它只做四件事：定位 hostfxr → 拉起运行时 → 找到托管侧 Boot / Tick 的入口 → 每帧调一次 Tick。
// 不链接 CoreCLR（hostfxr 是运行时 LoadLibrary 出来的），所以换 .NET 版本不用重编本模块。
// 任何一步失败都必须把"找过哪些路径 / 版本号"写进 LastError，由调用方打日志 —— 不许静默降级。
#include <CACore/CAModuleManager.h>
#include <ScriptHost/IScriptHost.h>
// 生成的绑定产物（render-interface-merge 任务 3.1）：跨接口登记表把逐接口产物一起带进来 ——
// 编号清单、EngineApi struct 与版本/大小断言、get_interface_api 路由、布局查询路由、装配代码
// 都在 `BindingRegistry.Bindings.h/.cpp` 里。宿主不再逐份 #include 接口产物，也不再手写
// `struct EngineApi`、手写编号常量或逐接口手写分派。
#include "BindingRegistry.Bindings.cpp"

// 宿主直持原生后端（5.1：桥与它的私有头一起退役）。CAInject_..._ExecuteGraph_scheduler 钩子
// 仍然由本文件提供，托管侧只看到生成的直绑表面。
#include <CRenderBackend.h>
#include <ThreadManager.h>

#define CA_IMPLEMENT_MODULE 1
#include <CACore/CAModuleImplementation.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#ifdef CASCADED_HAS_NETHOST
#include <nethost.h>
#include <coreclr_delegates.h>
#include <hostfxr.h>
#endif

namespace cs_script
{
	// 注入钩子用的线程管理器（csharp-native-direct-binding 2.2 / design D1）：`CreateRenderAdapter`
	// 从模块管理器取到真实 `CThreadManager` 后写这里；钩子只被生成产物的 thunk 调用。
	static thread_management::CThreadManager* s_threadManager = nullptr;
	// EngineApi 的字段、版本与大小断言都在生成的 `BindingRegistry.Bindings.h` 里（同一次生成发出
	// C++ struct、托管镜像与两侧常量）；宿主不再保留手写 struct / 版本常量 / 编号常量。
	//
	// 宿主角色登记（render-interface-merge 任务 3.1）：进主登记表的接口每个一行 ——
	// `true` = 要求默认实例，`false` = 目标类型表（允许空实例）。编号取自生成常量，不手抄数字。
	// 新增一个进主登记表的接口时，这里加一行（任务 5.1 列出的四处手写改动面之一）。
	struct BindingPolicyEntry
	{
		uint32_t id;
		bool requiresInstance;
	};
	constexpr BindingPolicyEntry kBindingPolicies[] = {
		{ kBindingInterfaceIdcawindow__IWindow, true },
		{ kBindingInterfaceIdcawindow__IWindowSystem, true },
		// csharp-native-direct-binding 2.4 / 4.1（design D12）：原生五类也要能从托管侧取表。
		// `CRenderBackend` 是引擎里那**一个**真实后端（默认实例）；`GPUGraph` / `ShaderStruct` /
		// `GPUTexture` / `WindowHandle` 是目标类型表（由工厂或句柄解析产出，允许空实例）。
		{ kBindingInterfaceIdgraphics_backend__CRenderBackend, true },
		{ kBindingInterfaceIdgraphics_backend__GPUGraph, false },
		{ kBindingInterfaceIdgraphics_backend__ShaderStruct, false },
		{ kBindingInterfaceIdgraphics_backend__GPUTexture, false },
		{ kBindingInterfaceIdgraphics_backend__WindowHandle, false },
		// 7.1（用户 2026-11-10 裁定）：缓冲对象直绑 —— 目标类型表（`CreateGPUBuffer` 产出，允许空实例）。
		{ kBindingInterfaceIdgraphics_backend__GPUBuffer, false },
	};
	// 接口实例登记表的一项：键是 **(接口编号, 实例名)**（任务 5.4 / design D10）。
	// 以前是一张 `void* s_interfaceInstances[8]`（下标就是编号），同一个接口登记第二份实现会
	// **覆盖**第一份 —— 两个 window system 并存时 `GetInterfaceInstance` 张冠李戴。
	struct InterfaceInstanceEntry
	{
		uint32_t id;
		std::string name;
		void* instance;
	};
	// 挂在永不释放的堆对象上：与绑定产物里的句柄表同一口径，避开静态析构顺序。
	static std::vector<InterfaceInstanceEntry>& InterfaceInstances()
	{
		static std::vector<InterfaceInstanceEntry>& table = *new std::vector<InterfaceInstanceEntry>();
		return table;
	}
	static std::mutex& InterfaceInstanceMutex()
	{
		static std::mutex& mutex = *new std::mutex();
		return mutex;
	}
	// 实例名允许为空（= 该接口的"默认那一份"），但不许是悬空指针：拷成 std::string 存起来。
	static std::string SafeInstanceName(char const* name)
	{
		return name == nullptr ? std::string() : std::string(name);
	}
	// 函数表大小从**审计表里的方法条数**算，不从生成物读 —— 读生成物等于用被检查物证明它自己。
	// `IWindowSystem` 的 16 个方法全部进表（本 change 解开了 10 个回调与 `GetMonitor`）。
	// 表头只有 abi_version(4) + size(4) 这 8 字节，之后每个槽 8 字节：4 个非方法槽（错误通道两槽、
	// 句柄解析/释放两槽）+ 16 个方法槽 + 2 个回调伴生槽（UnregisterCallback / ReportCallbackError）
	// = 24 槽 ⇒ 8 + 8 × 24 = 200。
	static_assert(sizeof(cawindow__IWindowSystemApi_v2) == 8u + 8u * 24u,
	              "cawindow::IWindowSystem 函数表应当正好 16 个方法 + 2 个回调伴生（4 个表头槽 + 每个方法槽 8 字节）");


	class CSharpHost final : public IScriptHost
	{
	public:
		// ---- 生成登记表的固定名字钩子（render-interface-merge 任务 3.1）----
		// 生成的 `BindingRegistry.Bindings.cpp` 按这些固定名字调用；具体行为仍是宿主手写。
		static void HostLog(int level, uint8_t const* utf8, uint64_t len)
		{
			OnManagedLog(level, utf8, len);
		}
		static int32_t HostEcho(int32_t value)
		{
			return OnManagedEcho(value);
		}
		static void HostReportError(char const* text)
		{
			if (s_instance != nullptr)
			{
				s_instance->m_lastError = text == nullptr ? std::string() : std::string(text);
			}
		}
		static void* HostLookupInstance(uint32_t interfaceId, char const* instanceName)
		{
			return s_instance == nullptr ? nullptr : s_instance->GetInterfaceInstance(interfaceId, instanceName);
		}
		static void HostCreateDefaultInstances()
		{
			if (s_instance != nullptr)
			{
				s_instance->CreateRenderAdapter(s_instance->m_moduleManager);
			}
		}
		static int32_t HostInterfaceRequiresInstance(uint32_t interfaceId)
		{
			for (BindingPolicyEntry const& policy : kBindingPolicies)
			{
				if (policy.id == interfaceId)
				{
					return policy.requiresInstance ? 1 : 0;
				}
			}
			return -1;
		}
		static uint32_t HostLayoutType(uint32_t interfaceId, uint32_t typeIndex, char* name, uint32_t nameCap,
									   uint64_t* sizeBytes, uint32_t* fieldCount)
		{
			return ca_bind_registry::GetBindingLayoutType(interfaceId, typeIndex, name, nameCap, sizeBytes,
														  fieldCount);
		}
		static uint32_t HostLayoutField(uint32_t interfaceId, uint32_t typeIndex, uint32_t fieldIndex, char* name,
										uint32_t nameCap, char* typeName, uint32_t typeCap, uint64_t* offsetBytes)
		{
			uint32_t result = ca_bind_registry::GetBindingLayoutField(interfaceId, typeIndex, fieldIndex, name,
																	  nameCap, typeName, typeCap, offsetBytes);
#if defined(_DEBUG)
			// 自检开关（只在 Debug 生效）：模拟"引擎侧编译时的布局与托管清单不一致"。
			if (result != 0u && offsetBytes != nullptr && SelfTestMode() == "layout-mismatch")
			{
				*offsetBytes += 8u;
			}
#endif
			return result;
		}

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

		// 把接口实例登记进来（任务 3.2 的"登记一行"，任务 5.4 起带实例名这一维）：
		// 编号定接口、实例名定"同一接口的第几份实现"。同一对 (编号, 实例名) 再登记就是覆盖。
		void SetInterfaceInstance(uint32_t interfaceId, char const* instanceName, void* instance) override
		{
			std::lock_guard<std::mutex> guard(InterfaceInstanceMutex());
			auto& table = InterfaceInstances();
			const std::string name = SafeInstanceName(instanceName);
			for (auto& entry : table)
			{
				if (entry.id == interfaceId && entry.name == name)
				{
					entry.instance = instance;
					return;
				}
			}
			table.push_back({interfaceId, name, instance});
		}

		void* GetInterfaceInstance(uint32_t interfaceId, char const* instanceName) const override
		{
			std::lock_guard<std::mutex> guard(InterfaceInstanceMutex());
			auto& table = InterfaceInstances();
			// `instanceName == nullptr` = 该接口的**默认那一份**（最先登记的那一条）。
			// 托管侧眼下只认默认那一份：它拿不到实例名（名字是引擎侧选的）。按名寻址是
			// 引擎与测试的入口，也是"同一接口多份实现"能被分开取到的唯一办法。
			if (instanceName == nullptr)
			{
				for (auto const& entry : table)
				{
					if (entry.id == interfaceId)
					{
						return entry.instance;
					}
				}
				return nullptr;
			}
			const std::string name = SafeInstanceName(instanceName);
			for (auto const& entry : table)
			{
				if (entry.id == interfaceId && entry.name == name)
				{
					return entry.instance;
				}
			}
			return nullptr;
		}

		uint32_t GetInterfaceApiForTest(uint32_t interfaceId, char const* instanceName, uint32_t version,
		                                uint32_t size, void* out, void** instanceOut) override
		{
			// 只给测试用：把跨边界的那个入口原样转一次（任务 4.2 要造出"未知编号"这次调用）。
			// 路由本体在生成的登记表里 —— 宿主的逐接口分支已经退役。
			return ca_bind_registry::GetInterfaceApi(interfaceId, instanceName, version, size, out, instanceOut);
		}

		// 模块 link 阶段（所有模块都登记完之后）被调用一次
		void Init(cacore::IModuleManager* pModuleManager)
		{
			s_instance = this;
			// 后端适配器的创建走生成装配代码的固定名工厂钩子
			// （FillEngineApi → CSharpHostBindingCreateDefaultInstances），仍发生在托管 Boot 之前；
			// 这里只把模块管理器留下来给钩子用。
			m_moduleManager = pModuleManager;
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

		char const* LastError() const override
		{
			// 托管侧的失败原因必须**每次问一遍**：它是另一侧进程内的静态字段，引擎侧自己看不见。
			// 以前这里直接返回 m_lastError，而它只在初始化失败时被写过 —— 于是"托管侧出错了"这件事
			// 在引擎侧永远读不到（实测：反向用例怎么改都绿）。
			//
			// 返回的是**固定数组**而不是 std::string::c_str()：调用方常把指针存起来稍后再用
			// （测试框架就是先拿 `char const*`、失败时再写进报告）。返回 c_str() 的话，下一次
			// 调用一旦重新赋值就可能重新分配，之前那个指针就悬空了 —— 实测报告里的 error 字段
			// 是 24 个 0xDD（MSVC debug 堆的"已释放"填充），就是这么来的。
			if (m_lastErrorFn != nullptr)
			{
				char buffer[512] = {};
				const int32_t got = m_lastErrorFn(buffer, static_cast<int32_t>(sizeof(buffer)));
				if (got > 0)
				{
					m_lastError = buffer;
				}
			}
			// **每一侧的错误都走这一个出口**（任务 4.2）：托管侧刚才写了就覆盖，没写就保留
			// 引擎侧自己记的那条（例如"未知接口编号 9"）。每次调用都从 `m_lastError` 拷到固定数组。
			if (!m_lastError.empty())
			{
				std::size_t const length = m_lastError.size() < sizeof(m_lastErrorText) - 1u
				                               ? m_lastError.size()
				                               : sizeof(m_lastErrorText) - 1u;
				std::memcpy(m_lastErrorText, m_lastError.c_str(), length);
				m_lastErrorText[length] = '\0';
			}
			return m_lastErrorText;
		}

		void Shutdown() override
		{
#ifdef CASCADED_HAS_NETHOST
			// 关闭阶段托管侧还可能回调过来（实测 hostfxr_close 期间 coreclr 会再调一次 log），
			// 那时进程里的日志设施已经/正在销毁，再走 CA_LOG 会崩在 CA_LOG 里。
			// 所以先把开关立起来，让回调改走 stderr。
			s_shuttingDown = true;
			// 托管注销（本 change 任务 4.1）：`hostfxr_close` **之前**调用，由它释放窗口系统表面对象
			// 并注销仍存活的回调。没有这个入口，关闭路径只是把引用置空，原生回调与 GCHandle 都还在。
			if (m_shutdown != nullptr && m_runtimeContext != nullptr)
			{
				const int32_t shutdownResult = m_shutdown();
				m_shutdown = nullptr;
				CA_LOG("CSharpHost：托管 Shutdown 返回 {}（0 = 已释放表面对象并注销回调）", shutdownResult);
				fprintf(stderr, "[CSharpHost] 托管 Shutdown 返回 %d\n", shutdownResult);
				fflush(stderr);
			}
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
		// 模块管理器（render-interface-merge 任务 3.1）：生成的装配代码通过固定名工厂钩子回到
		// 这里，钩子需要一个稳定的管理器指针。
		cacore::IModuleManager* m_moduleManager = nullptr;

		// 用模块管理器里的真实后端与线程管理器登记**原生**渲染后端（编号 = 生成的 CRenderBackend
		// 常量，默认实例）。5.1：桥退役后宿主直持原生对象，不再 new 适配器。
		// 缺少依赖时**不登记**编号，并把原因留在 m_lastError（不造空壳、不伪造成功）；
		// 未启用渲染探针的窗口路径不受影响。
		void CreateRenderAdapter(cacore::IModuleManager* pModuleManager)
		{
			auto* backend = pModuleManager != nullptr
				? pModuleManager->GetInstance<graphics_backend::CRenderBackend>()
				: nullptr;
			auto* threads = pModuleManager != nullptr
				? pModuleManager->GetInstance<thread_management::CThreadManager>()
				: nullptr;
			// 注入钩子（csharp-native-direct-binding 2.2 / design D1）要用它新建调度器：
			// `ExecuteGraph(TaskScheduler*, graph)` 的调度器参数由引擎提供，脚本永远看不到它。
			s_threadManager = threads;
			if (backend == nullptr || threads == nullptr)
			{
				m_lastError = std::string("渲染后端未登记：缺少 ") +
					(backend == nullptr ? "CRenderBackend" : "") +
					(backend == nullptr && threads == nullptr ? " 与 " : "") +
					(threads == nullptr ? "CThreadManager" : "");
				CA_LOG_ERR("CSharpHost：{}", m_lastError.c_str());
				return;
			}
			SetInterfaceInstance(kBindingInterfaceIdgraphics_backend__CRenderBackend, nullptr, backend);
			// 这行必须报**登记表的实际读数**：csharp-render-shader-constants 任务 3.1 的变异实验里
			// 临时注释掉上面那行登记，日志却照样打"已登记"——文本在说谎，把缺口遮住了。现在直接把
			// 查表结果打在后面，登记没发生时读数是 0，一眼可见。
			void* registered = GetInterfaceInstance(kBindingInterfaceIdgraphics_backend__CRenderBackend, nullptr);
			CA_LOG("CSharpHost：原生渲染后端{}（编号 {}；后端地址 {:X}；登记表读数 {:X}）",
				registered == backend ? "已登记" : "未登记", kBindingInterfaceIdgraphics_backend__CRenderBackend,
				reinterpret_cast<uintptr_t>(backend), reinterpret_cast<uintptr_t>(registered));
		}
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
			// 装配（render-interface-merge 任务 3.1）：由生成的登记表填字段 —— 包括按固定名字回调
			// 宿主的默认实例工厂、按宿主角色登记表区分默认实例/目标类型表、填入两侧同源的编号常量。
			ca_bind_registry::FillEngineApi(api);
#if defined(_DEBUG)
			// 自检开关（只在 Debug 生效）：让层 ③ 的失败路径测试不必临时改源码就能构造"ABI 不符"。
			// 用法见 CSharpHost/README.md 的环境变量表。
			if (SelfTestMode() == "abi-mismatch")
			{
				api.abi_version = kEngineApiVersion + 1;
				CA_LOG_ERR("CSharpHost 自检开关已生效：故意把 abi_version 改成 {}（真实值 {}）",
					api.abi_version, kEngineApiVersion);
			}
#endif
			CA_LOG("CSharpHost：引擎侧 OnGetInterfaceApi 的地址 = {:X}，函数表 {} 字节",
				reinterpret_cast<uintptr_t>(api.get_interface_api), api.size);

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

			// 失败原因的出口（`csharp-window-creation` 任务 2.7）：托管侧把它写成静态字段，
			// 只有这一个函数能取出来；解析失败**不算致命** —— 只是拿不到细节文本，功能照旧。
			rc = m_loadAssembly(assemblyPath.c_str(), typeName.c_str(), L"GetLastError",
				UNMANAGEDCALLERSONLY_METHOD, nullptr,
				reinterpret_cast<void**>(&m_lastErrorFn));
			if (rc != 0)
			{
				m_lastErrorFn = nullptr;
			}

			// 托管关闭入口（本 change 任务 4.1）：`Shutdown()` 里要在 `hostfxr_close` 之前调用它，
			// 由它释放窗口系统表面对象并注销仍存活的回调。解析失败要留下可见记录，不能静默跳过。
			rc = m_loadAssembly(assemblyPath.c_str(), typeName.c_str(), L"Shutdown",
				UNMANAGEDCALLERSONLY_METHOD, nullptr,
				reinterpret_cast<void**>(&m_shutdown));
			if (rc != 0 || m_shutdown == nullptr)
			{
				m_shutdown = nullptr;
				fprintf(stderr, "[CSharpHost] 解析托管入口 Shutdown 失败（返回 %d）：关闭时不会执行托管注销\n", rc);
				fflush(stderr);
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

		// 逐接口的取表 / 布局查询分派已退役（render-interface-merge 任务 3.1）：路由在生成的
		// BindingRegistry.Bindings.cpp 里，宿主只保留上面那些固定名字的实现钩子。

		using BootFn = int32_t (*)(EngineApi*);
		using TickFn = int32_t (*)(double);
		// 把托管侧的失败原因拷出来（0 = 没有错误，正数 = 写入的字节数）
		using LastErrorFn = int32_t (*)(char*, int32_t);
		// 托管关闭入口（本 change 任务 4.1）：释放窗口系统表面对象并注销仍存活的回调。
		using ShutdownFn = int32_t (*)();

		bool m_bootSucceeded = false;
		BootFn m_boot = nullptr;
		TickFn m_tick = nullptr;
		ShutdownFn m_shutdown = nullptr;
		LastErrorFn m_lastErrorFn = nullptr;
		int32_t m_expectedTick = 0;
		mutable std::string m_lastError;
		// LastError() 返回它：固定数组，指针永远有效（内容可被下一次调用覆盖，但不会悬空）
		mutable char m_lastErrorText[512] = {};
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
	};

	int32_t CSharpHost::s_echoCalls = 0;
	bool CSharpHost::s_shuttingDown = false;
	CSharpHost* CSharpHost::s_instance = nullptr;
}

// ---- 生成登记表的固定名字钩子实现（render-interface-merge 任务 3.1）----
// 生成的 `BindingRegistry.Bindings.h` 声明这些名字、装配代码按名字调用；行为本体的实现仍在
// 上面的 CSharpHost 里（log / echo / 实例查找 / 错误出口 / 默认实例工厂 / 角色登记 / 布局自检）。
extern "C" void CSharpHostManagedLog(int level, uint8_t const* utf8, uint64_t len)
{
	cs_script::CSharpHost::HostLog(level, utf8, len);
}

extern "C" int32_t CSharpHostManagedEcho(int32_t value)
{
	return cs_script::CSharpHost::HostEcho(value);
}

extern "C" void CSharpHostBindingReportError(char const* text)
{
	cs_script::CSharpHost::HostReportError(text);
}

extern "C" void* CSharpHostBindingLookupInstance(uint32_t interfaceId, char const* instanceName)
{
	return cs_script::CSharpHost::HostLookupInstance(interfaceId, instanceName);
}

extern "C" void CSharpHostBindingCreateDefaultInstances(void)
{
	cs_script::CSharpHost::HostCreateDefaultInstances();
}

extern "C" int32_t CSharpHostBindingRequiresInstance(uint32_t interfaceId)
{
	return cs_script::CSharpHost::HostInterfaceRequiresInstance(interfaceId);
}

extern "C" uint32_t CSharpHostBindingLayoutType(uint32_t interfaceId, uint32_t typeIndex, char* name,
												uint32_t nameCap, uint64_t* sizeBytes, uint32_t* fieldCount)
{
	return cs_script::CSharpHost::HostLayoutType(interfaceId, typeIndex, name, nameCap, sizeBytes, fieldCount);
}

extern "C" uint32_t CSharpHostBindingLayoutField(uint32_t interfaceId, uint32_t typeIndex, uint32_t fieldIndex,
												 char* name, uint32_t nameCap, char* typeName, uint32_t typeCap,
												 uint64_t* offsetBytes)
{
	return cs_script::CSharpHost::HostLayoutField(interfaceId, typeIndex, fieldIndex, name, nameCap, typeName,
												  typeCap, offsetBytes);
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

// 参数注入钩子（csharp-native-direct-binding 2.2 / design D1）：`CRenderBackend::ExecuteGraph` 的
// 调度器参数由引擎在这里提供 —— 每次调用新建一个调度器，用一个线程局部强引用保活到下一次调用
// （ExecuteGraph 是同步提交，返回时调度器不再被使用）。线程管理器缺失时返回空指针，thunk 会把它
// 当空值传给原生（后端对空调度器的行为不在本批改动面内）。
extern "C" void* CAInject__graphics_backend__CRenderBackend__ExecuteGraph__scheduler()
{
	static thread_local castl::shared_ptr<thread_management::TaskScheduler> s_scheduler;
	if (cs_script::s_threadManager == nullptr)
	{
		s_scheduler.reset();
		return nullptr;
	}
	s_scheduler = cs_script::s_threadManager->NewScheduler();
	return s_scheduler.get();
}

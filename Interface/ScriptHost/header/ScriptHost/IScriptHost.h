#pragma once
#include <stdint.h>

// 引擎与托管侧（C#）之间唯一的手写接口。
// 它只暴露"有没有起来"与"每帧调一次"，任何引擎类型都不从这里过界
// （跨边界的形状规矩见 openspec/specs/csharp-bindable-surface/spec.md）。
namespace cs_script
{
	class IScriptHost
	{
	public:
		// 托管运行时与程序集是否都已加载成功
		virtual bool IsAvailable() const = 0;

		// 引擎每帧调一次；返回托管侧 Tick 的回值（0 = 成功，非 0 = 错误码）
		virtual int32_t Tick(double deltaSeconds) = 0;

		// 最近一次失败的原因（UTF-8，指向内部存储，调用方不得释放）；成功时返回空串
		virtual char const* LastError() const = 0;

		// 关闭：释放运行时句柄与程序集引用（由模块的 TryRelease 调用；可重复调）
		virtual void Shutdown() = 0;

		// 把某个接口的实例登记给托管侧（任务 3.2 的"登记一行"）。
		// `interfaceId` 是引擎侧约定的编号：**1 = IWindow**（见 CSharpHost/README.md 的表）。
		// 托管侧通过函数表里的 get_interface_instance / get_interface_api 拿到它。
		virtual void SetInterfaceInstance(uint32_t interfaceId, void* instance) = 0;

		// **只给测试用**：直接调一次跨边界的取表入口（`EngineApi::get_interface_api`）。
		// 存在的理由：那个入口平时只有托管侧能拿到，而"未知接口编号必须报错、不许静默返回 0"
		// 这条判据需要一个能在引擎侧造出这次调用的入口（`csharp-window-creation` 任务 4.2）。
		// 返回值与 `EngineApi::get_interface_api` 同义：0 = 失败（原因在 `LastError()` 里）。
		// 生产代码 MUST NOT 调用它。
		virtual uint32_t GetInterfaceApiForTest(uint32_t interfaceId, uint32_t version, uint32_t size,
		                                        void* out, void** instanceOut) = 0;

		virtual ~IScriptHost() = default;
	};
}

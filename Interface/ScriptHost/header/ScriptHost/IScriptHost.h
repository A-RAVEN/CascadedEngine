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

		virtual ~IScriptHost() = default;
	};
}

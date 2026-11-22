#pragma once
#include <CodeGenMeta.h>
#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>
#include <stdexcept>
#include <string>
#include "IResource.h"

namespace resource_management
{
	// 资源工厂：对 C++ 与 C# 是对称的 —— C++ 类型**原生继承**它写工厂；C# 类型继承生成的反向
	// 基类 `ResourceFactoryBase`（反向代理包装）。两种语言的工厂都按类型名注册进工厂注册表。
	//
	// **Create() 不收 ResourceManagingSystem 参数**（绕开不可绑的协作方）：工厂只负责构造资源对象，
	// 注册进资源表由系统负责。Create() 失败 MUST 抛异常（C++ 原生工厂直接抛；C# 工厂的异常经桥
	// 转码成同一个异常类型再抛），MUST NOT 返回 nullptr 来表示失败。
	//
	// 可选的后缀方法（旧 1:1 导入器那套源后缀 / 目标后缀）本批不定义：反向投影会要求每份
	// C# 工厂都实现它，而本批的资源路径不消费这两个值。需要时按"反向接口加一个虚方法 + 补审计
	// 表一行 + 跑 --update-ids"的流程加。
	CA_BIND_REVERSE()
	class IResourceFactory
	{
	public:
		virtual ~IResourceFactory() {}
		virtual castl::string GetResourceType() const = 0;
		virtual IResource* Create() = 0;
	};

	// 工厂创建失败（未注册的类型 / Create 抛异常 / Create 返回空）。消息里点名类型名与原始原因。
	class resource_creation_error : public std::runtime_error
	{
	public:
		explicit resource_creation_error(std::string const& message)
			: std::runtime_error(message)
		{
		}
	};

	// 反向工厂的"释放强句柄"回调：由**创建代理的那个模块**提供（生成的 `__ReleaseImpl`）。
	// 注册表注销 / 替换工厂时调它 —— 它放掉句柄表里的强引用，代理析构 → releaseUser → GCHandle.Free。
	//
	// 为什么不是"句柄反查重建 shared_ptr"：句柄表是**生成在头里的 inline + 函数级 static**，
	// EXE 与 DLL 各有一份；`shared_ptr` 也不能跨模块传。跨模块能用的只有"句柄 + 创建模块的
	// 解析/释放入口"：解析用反向函数表的 `ResolveImpl`（拿借用指针），释放用这里这个回调。
	// 单一强所有者仍然是句柄表那一条（注册表只持借用指针 + 释放能力，MUST NOT 丢句柄）。
	using ResourceFactoryRelease = int32_t (*)(uint64_t handle);
}

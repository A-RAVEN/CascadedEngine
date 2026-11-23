#pragma once
#include <CASTL/CAString.h>
#include <stdint.h>
// 绑定标注宏（CA_BIND）：这个头从 csharp-resource-asset-foundation 起进绑定面。
#include <CodeGenMeta.h>

// C# 可调的资源注册表窄面（csharp-resource-asset-foundation / design D2）。
//
// 定位：这是**宿主层门面**，不是注册表本体。实现放在 CSharpHost，经模块管理器取
// `resource_management::ResourceManagingSystem` 后逐个方法**委托**给它：
//   * 注册表仍是资源对象的**单一强所有者**（`m_PathToResource` 那一份 shared_ptr）；
//   * 门面自己不持第二份强引用，也不另建对象表 —— 方法返回 void / bool / string，
//     资源对象怎么拿由 C# 工厂的跟踪队列负责（design D4）；
//   * C++ 侧要对象仍直接用 `ResourceManagingSystem::CreateResource`（返回 shared_ptr），
//     这条窄面不改变任何既有所有权语义。
//
// 为什么门面不直接绑注册表本体（design D1）：工厂注册的句柄入口收裸指针 + 裸函数指针，
// 形状不收；解析句柄要 `ca_bind_registry`，放进 GeneralResources 等于新增
// 「核心模块 → 绑定层」依赖。宿主已经持有这两个能力，所以门面坐宿主层。
//
// 形状核对（design D2）：`castl::string` 入参与返回、`uint64_t`、`bool`、`void` 全部是
// 正向生成器已实现的形状（Tools/CodeGen/README.md 投影矩阵），本批不改生成器。
namespace cs_script
{
	CA_BIND()
	class IResourceRegistry
	{
	public:
		virtual ~IResourceRegistry() {}

		// 工厂注册（design D3）：收反向工厂的强句柄，宿主经生成登记表的 `ResolveImpl` 解析成
		// 借用指针后委托注册表既有句柄入口；解析失败必须点名报错，不许登记空工厂。
		// **注册成功后强句柄的处置权归注册表**（注销 / 替换时释放），C# 不得在注册期间再释放同一句柄。
		virtual void RegisterFactory(castl::string const& typeName, uint64_t factoryHandle) = 0;

		// 类型名是否已登记工厂（与注册表读数一致）
		virtual bool HasFactory(castl::string const& typeName) const = 0;

		// 注销工厂：放掉注册表保留的那份强引用（反向代理析构 → 托管 GCHandle 释放）
		virtual bool UnregisterFactory(castl::string const& typeName) = 0;

		// 新建资源（**不是加载**）：委托注册表 `CreateResource`（查工厂 → Create → 注册）。
		// 同路径已有条目时**先拒绝**（design D2 的守卫）：注册表 get_or_create 会留旧弃新，
		// 门面 void 返回若静默放行，C# 队列拿到的是析构中的孤儿对象。替换正路是先释放再新建。
		virtual void CreateResource(castl::string const& typeName, castl::string const& path) = 0;

		// 路径是否已有资源条目
		virtual bool HasResource(castl::string const& path) const = 0;

		// 释放一个资源条目（放掉注册表那份唯一强引用；反向代理在这里析构）
		virtual bool ReleaseResource(castl::string const& path) = 0;

		// 无条件全量落盘（导入器批确立的规矩：不按脏标记跳过）
		virtual void SerializeAll() = 0;

		// 资源根路径（与注册表当前设置一致）
		virtual castl::string GetResourceRootPath() const = 0;

		// 相对路径拼成绝对路径（与注册表 `GetResourceFullPath` 逐字一致）
		virtual castl::string GetResourceFullPath(castl::string const& path) const = 0;
	};
}

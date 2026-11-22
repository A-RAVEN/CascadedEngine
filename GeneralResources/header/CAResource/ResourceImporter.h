#pragma once
#include <CodeGenMeta.h>
#include <CASTL/CAString.h>
#include "IResource.h"

namespace resource_management
{
	// 目录级导入器（v3 拉取式，resource-importer-refactor）。
	//
	// 分工：**导入器只把源目录变成资源对象，不注册**；注册、替换与落盘全部由管线
	// `ResourceImportingSystem::ScanSourceDirectory` 做。导入器不要求 1:1 —— 一次 Produce()
	// 可以交出任意多条产出（一个 fbx 出网格 + 材质 + 贴图、着色器拉 include 都属于这一类）。
	//
	// 接口形状（**必须落在 CodeGen 反向投影现有的四种形状里，不扩生成器**）：
	//   * `castl::string` 返回 + 无参   → 两段式 `<方法>Len` / `Copy<方法>` 槽
	//   * `bool` / 标量返回             → 直接槽返回
	//   * 返回**另一个反向接口**的裸指针 → C# 侧拿到那个接口的抽象基类，代理用 CreateProxy 造原生对象
	// 反向形参只支持标量，所以这里**没有**「把输出收集器传进来」的 push 形状；产出靠拉取。

	// 本次扫描的根（**不进绑定面**，没有虚函数、没有 CA_BIND 标注）：管线在调用 Produce() 之前
	// 把「本次扫描实际使用的源根 / 目标根」写进来。
	//
	// 为什么要有这一层：拉取式接口不能把目录当形参传（反向形参只支持标量），而原生导入器（两个
	// 着色器导入器）今天依赖 `ScanSourceDirectory` 传进来的那个源目录。于是约定成：
	//   * `GetSourceDirectory()` / `GetDestDirectory()` 返回**空** = 导入器跟随管线传入的根；
	//     管线把有效根写进这里，原生导入器在 Produce() 里读 `GetScanSourceDirectory()`。
	//   * C# 反向实现读不到这两个值（托管代码没有入口），所以 C# 导入器 MUST 在自己的
	//     GetSourceDirectory()/GetDestDirectory() 里声明目录 —— 不能留空。
	// 存储放在接口对象的基类里（不是全局静态）：跨模块看到的必须是同一个对象里的同一份数据。
	class ResourceImporterScanContext
	{
	public:
		void SetScanDirectories(castl::string const& sourceDirectory, castl::string const& destDirectory)
		{
			m_ScanSourceDirectory = sourceDirectory;
			m_ScanDestDirectory = destDirectory;
		}
		castl::string const& GetScanSourceDirectory() const { return m_ScanSourceDirectory; }
		castl::string const& GetScanDestDirectory() const { return m_ScanDestDirectory; }

	protected:
		castl::string m_ScanSourceDirectory;
		castl::string m_ScanDestDirectory;
	};

	class ResourceImporterProductions;

	// 唯一导入器接口（取代旧的两套 1:1 导入器抽象与它们的模板；那两套已经退役）。
	CA_BIND_REVERSE()
	class ResourceImporter : public ResourceImporterScanContext
	{
	public:
		virtual ~ResourceImporter() {}
		virtual castl::string GetTags() const = 0;
		// 导入器自己固定的源根；**空 = 跟随 ScanSourceDirectory 传入的根**（见基类注释）。
		virtual castl::string GetSourceDirectory() const = 0;
		// 导入器自己固定的目标根；空 = 跟随资源系统的资源根（同样见基类注释）。
		virtual castl::string GetDestDirectory() const = 0;
		// 扫一遍源、交出产出迭代器；没有产出时返回空迭代器（HasNext() 恒假）。返回的对象归管线所有。
		virtual ResourceImporterProductions* Produce() = 0;
	};

	// 一次 Produce() 的产出迭代器（拉取式）：管线先问 HasNext()，为真才取一条
	// （NextPath() + NextResource()），取完再问下一次。**每条产出一条资源**。
	//
	// 所有权：NextResource() 交出的对象所有权归管线（管线包 `shared_ptr(ptr, delete)` 后注册，
	// 与 CreateResource 同构）；C# 反向实现的返回值由生成代理用 `CreateProxy` 造出原生代理，
	// 同样交给管线。导入器 MUST NOT 自己注册 / 释放资源（五写注册表入口一律不许出现）。
	CA_BIND_REVERSE()
	class ResourceImporterProductions
	{
	public:
		virtual ~ResourceImporterProductions() {}
		// 还有没有下一条产出。
		virtual bool HasNext() = 0;
		// 下一条产出的资源路径：相对资源根、也是注册键与落盘路径（例如 "VulkanShaderLibrary.shLib"）。
		// 只在 HasNext() 为真时调用；每次推进一条。
		virtual castl::string NextPath() = 0;
		// 下一条产出的资源对象（所有权交给管线）。
		virtual IResource* NextResource() = 0;
	};
}

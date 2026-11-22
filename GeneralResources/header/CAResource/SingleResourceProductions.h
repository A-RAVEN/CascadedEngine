#pragma once
#include "ResourceImporter.h"

namespace resource_management
{
	// C++ 1:1 导入器用的单条产出迭代器：只交一条 (相对路径, 资源对象)，取一次就空。
	// 着色器导入器（一个源目录 → 一个 ShaderLibrary）用它；C# 反向实现不用它（托管侧自己
	// 实现 `ResourceImporterProductionsBase`，见生成产物）。
	class SingleResourceProductions : public ResourceImporterProductions
	{
	public:
		SingleResourceProductions(castl::string relativePath, IResource* resource)
			: m_RelativePath(relativePath), m_Resource(resource)
		{
		}

		virtual bool HasNext() override { return m_Resource != nullptr; }
		virtual castl::string NextPath() override { return m_RelativePath; }
		virtual IResource* NextResource() override
		{
			IResource* produced = m_Resource;
			m_Resource = nullptr;
			return produced;
		}

	private:
		castl::string m_RelativePath;
		IResource* m_Resource = nullptr;
	};
}

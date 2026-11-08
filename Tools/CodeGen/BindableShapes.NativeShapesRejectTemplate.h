#pragma once
// 拒绝夹具（codegen-native-binding-prereqs 任务 2.2）：被采集的绑定类里有一个**模板成员函数**，
// 且没有 CA_BIND_SKIP 显式排除它。生成器 MUST 非零退出并点名"① 模板/泛型"。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
#include <stdint.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	CA_BIND()
	class IBindRejectTemplateMethod
	{
	public:
		// ① 模板方法：本批不做模板单态化绑定。
		template<typename T>
		T GetAs() const;

		virtual uint32_t GetValue() const = 0;
		virtual ~IBindRejectTemplateMethod() = default;
	};
}

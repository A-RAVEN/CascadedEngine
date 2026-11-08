#pragma once
// 拒绝夹具（codegen-native-binding-prereqs 任务 2.3）：被采集的绑定类里有一个**返回含容器记录**的
// 静态工厂。生成器 MUST 非零退出并点名"返回位置容器"，MUST NOT 把它标成可绑。
//
// 这个头只被 CoreTests 的拒绝探针起一次 CodeGen.exe，不参与构建的绑定生成。
// 用 std::vector 而不是 castl::vector：拒绝探针只传 CACore/header 与 Tools/CodeGen 两条 include，
// 不引 CASTL；容器识别对两者是同一套规范化口径。
#include <stdint.h>
#include <vector>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	// 返回位置的记录：成员含一层容器 —— 本批不做返回位置的容器投影。
	struct RejectContainerRecord
	{
		std::vector<uint32_t> values;
	};

	CA_BIND()
	class IBindRejectFactoryReturn
	{
	public:
		// 静态工厂返回含容器的记录 ⇒ 拒绝（"返回位置容器"）。
		static RejectContainerRecord MakeContainer();

		virtual uint32_t GetValue() const = 0;
		virtual ~IBindRejectFactoryReturn() = default;
	};
}

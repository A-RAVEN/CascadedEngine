#pragma once
// 反向接口夹具（csharp-reverse-interface-binding 3.1）：C# 实现 C++。
//
// 它只做两件事：① 给 CoreTests 的结构判据提供"生成出来的代理 / glue / 句柄 kind"（C++ 侧直接
// 提供一组函数指针当假实现）；② 给 tester 的行为判据提供"C# 继承抽象基类 → 经代理被 C++ 调到"
// 的素材。夹具进编号锁的夹具段（**不进宿主路由**）。
#include <CodeGenMeta.h>
#include <CASTL/CAString.h>
#include <stdint.h>

namespace ca_bind_fixture
{
	CA_BIND_REVERSE()
	class IBindReverseShape
	{
	public:
		virtual ~IBindReverseShape() {}
		// 标量返回 + 标量参数：调一次槽收返回值。
		virtual int32_t Add(int32_t left, int32_t right) = 0;
		// 两段式字符串返回：`DescribeLen` + `CopyDescribe`（一个虚方法投影两条槽）。
		virtual castl::string Describe() const = 0;
		// void 返回 + 无参。
		virtual void Reset() = 0;
		// bool 返回（C ABI 是 int32，托管侧仍是 bool）。
		virtual bool IsReady() const = 0;
		// 本批不绑（反向 Deserialize 要先有 IOBatch 长度查询）：代理里出抛 1013 的实现。
		virtual void Deserialize(void* ignoredBatch) = 0;
		CA_BIND_SKIP(Deserialize, "本批不支持反向读，另立批");
	};
}

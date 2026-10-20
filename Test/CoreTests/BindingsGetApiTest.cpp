// 绑定函数表的取表校验（`csharp-bindings-generator` 任务 2.2 的两条探针，常驻在 CoreTests 里）。
//
// 判的是三件事：
//   ① 版本与大小都对 → `GetApi` 返回真实表大小，且表里的 `abi_version` / `size` 与实现一致；
//   ② 版本不对 → 返回 0，且**调用方缓冲区一个字节都没被写**（先填 0xCD 再逐字节查）；
//   ③ 大小不对 → 同上。
//
// 为什么要有 ②③：只验"正确的能通过"是恒真判据 —— 取表函数把校验删掉也照样绿。

#include "BindableShapes.h"

#include "IBindRow01.Bindings.cpp"

#include <cstdio>
#include <cstring>

namespace
{
	class FakeRow01 final : public ca_bind_fixture::IBindRow01
	{
	public:
		int32_t GetInt32(int32_t value) override
		{
			return value + 1;
		}

		bool GetBool(bool value) override
		{
			return !value;
		}

		ca_bind_fixture::EBindMode GetMode(ca_bind_fixture::EBindMode mode) override
		{
			return mode;
		}
	};

	bool AllBytesAre(unsigned char const* bytes, std::size_t count, unsigned char expected)
	{
		for (std::size_t index = 0; index < count; ++index)
		{
			if (bytes[index] != expected)
			{
				return false;
			}
		}
		return true;
	}
}

bool RunBindingsGetApiTest()
{
	const uint32_t realSize = static_cast<uint32_t>(sizeof(IBindRow01Api_v1));
	IBindRow01Api_v1 table;
	std::memset(&table, 0xCD, sizeof(table));

	// ① 正确的版本与大小
	const uint32_t returned = IBindRow01_GetApi(1u, realSize, &table);
	if (returned != realSize)
	{
		std::printf("  GetApi 正确调用：期望返回 %u，实际 %u\n", realSize, returned);
		return false;
	}
	if (table.abi_version != 1u || table.size != realSize)
	{
		std::printf("  表头不符：abi_version=%u（期望 1）、size=%u（期望 %u）\n", table.abi_version,
		            table.size, realSize);
		return false;
	}

	// ② 版本不对：返回 0 且缓冲区不被写
	IBindRow01Api_v1 untouchedForVersion;
	std::memset(&untouchedForVersion, 0xCD, sizeof(untouchedForVersion));
	if (IBindRow01_GetApi(2u, realSize, &untouchedForVersion) != 0u)
	{
		std::puts("  GetApi 版本不符时没有返回 0");
		return false;
	}
	if (!AllBytesAre(reinterpret_cast<unsigned char const*>(&untouchedForVersion),
	                 sizeof(untouchedForVersion), 0xCD))
	{
		std::puts("  GetApi 版本不符时仍然写了调用方缓冲区");
		return false;
	}

	// ③ 大小不对：同样返回 0 且不写
	IBindRow01Api_v1 untouchedForSize;
	std::memset(&untouchedForSize, 0xCD, sizeof(untouchedForSize));
	if (IBindRow01_GetApi(1u, realSize - 8u, &untouchedForSize) != 0u)
	{
		std::puts("  GetApi 大小不符时没有返回 0");
		return false;
	}
	if (!AllBytesAre(reinterpret_cast<unsigned char const*>(&untouchedForSize), sizeof(untouchedForSize),
	                 0xCD))
	{
		std::puts("  GetApi 大小不符时仍然写了调用方缓冲区");
		return false;
	}

	// 顺带证明"表里的函数指针真的调到实现"（否则上面三条都能在空表上绿）
	FakeRow01 object;
	if (table.GetInt32(&object, 41) != 42)
	{
		std::puts("  通过函数表调用 GetInt32 回值不对");
		return false;
	}
	if (table.GetBool(&object, true) != 0)
	{
		std::puts("  通过函数表调用 GetBool（true 应回 0）不对");
		return false;
	}

	return true;
}

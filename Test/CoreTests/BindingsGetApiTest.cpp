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
	const uint32_t realSize = static_cast<uint32_t>(sizeof(IBindRow01Api_v2));
	IBindRow01Api_v2 table;
	std::memset(&table, 0xCD, sizeof(table));

	// ① 正确的版本与大小
	const uint32_t returned = IBindRow01_GetApi(2u, realSize, &table);
	if (returned != realSize)
	{
		std::printf("  GetApi 正确调用：期望返回 %u，实际 %u\n", realSize, returned);
		return false;
	}
	if (table.abi_version != 2u || table.size != realSize)
	{
		std::printf("  表头不符：abi_version=%u（期望 2）、size=%u（期望 %u）\n", table.abi_version,
		            table.size, realSize);
		return false;
	}

	// ② 版本不对：返回 0 且缓冲区不被写。用 1u —— 那是错误通道进表之前的版本号
	//    （`csharp-window-creation` 任务 5.10 把版本从 1 升到 2），这条同时证明旧产物会被拦。
	IBindRow01Api_v2 untouchedForVersion;
	std::memset(&untouchedForVersion, 0xCD, sizeof(untouchedForVersion));
	if (IBindRow01_GetApi(1u, realSize, &untouchedForVersion) != 0u)
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
	IBindRow01Api_v2 untouchedForSize;
	std::memset(&untouchedForSize, 0xCD, sizeof(untouchedForSize));
	if (IBindRow01_GetApi(2u, realSize - 8u, &untouchedForSize) != 0u)
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

	// 任务 2.1：上面那条 `GetBool(&object, true) != 0` 对**桩也成立**（桩返回 0、FakeRow01::GetBool(true)
	// 也返回 0，两个 0 相等），所以它造不出红态。这里补"实现返回真 ⇒ C ABI 读到 1"这一半：
	// 传 false 时实现返回 !false = true，桩只会返回 0，这条在桩上必红。
	if (table.GetBool(&object, false) != 1)
	{
		std::puts("  通过函数表调用 GetBool（false 应回 1）不对 —— 桩会在这里露馅");
		return false;
	}

	// 任务 2.2：枚举必须经函数表读到真值（只靠"thunk 里有 static_cast"这种文本判据，
	// 写成 `static_cast<int32_t>(object->GetMode(mode)); return {};` 也能骗过）。
	for (int32_t mode = 0; mode < 3; ++mode)
	{
		if (table.GetMode(&object, mode) != mode)
		{
			std::puts("  通过函数表调用 GetMode 回值不对（枚举没有真的走实现）");
			return false;
		}
	}

	return true;
}

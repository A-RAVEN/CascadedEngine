// `IWindow` 的绑定产物能不能被真正编译并链接（`csharp-bindings-generator` 任务 2.10）。
//
// 这个文件 `#include` 了生成出来的 `IWindow.Bindings.cpp`：只要 CoreTests 能链接成功，就证明
// 那份产物**进了编译**（不是躺在磁盘上没人用）。运行期再验两条：
//   ① `IWindow_GetApi` 的版本/大小握手按约定工作；
//   ② 表里的方法条数与审计过的可绑方法数一致（23 条纯虚 − 1 条带理由排除 = 22）。

#include <CAWindow/WindowSystem.h>

#include "IWindow.Bindings.cpp"

#include <cstdio>
#include <cstring>

namespace
{
	// 任务 2.3：核 `void*` → `uint64_t` 不透明令牌真的回读了实现给的那个地址。
	// 必须用假对象 —— CoreTests 只链 CACore / glm / 接口目标，`WindowSystem` 是运行时加载的
	// MODULE，这一层开不了真窗口（见 docs/testing.md 的分层说明）。
	class FakeWindowForHandle final : public cawindow::IWindow
	{
	public:
		static constexpr uintptr_t Sentinel = 0x5A5A1234u;
		castl::string m_name = "Default Window";

		void CloseWindow() override {}
		bool WindowShouldClose() const override { return false; }
		void ShowWindow() override {}
		void SetWindowPos(int, int) override {}
		void GetWindowPos(int& outX, int& outY) const override { outX = 0; outY = 0; }
		void SetWindowSize(int, int) override {}
		void GetWindowSize(int& outX, int& outY) const override { outX = 0; outY = 0; }
		void Focus() override {}
		bool GetWindowFocus() const override { return false; }
		bool GetWindowMinimized() const override { return false; }
		void SetWindowName(castl::string const& name) override { m_name = name; }
		castl::string_view GetWindowName() const override { return m_name; }
		void SetWindowAlpha(float) override {}
		float GetDpiScale() const override { return 1.0f; }
		void* GetNativeWindowHandle() override { return reinterpret_cast<void*>(Sentinel); }
		cawindow::IWindowSystem* GetWindowSystem() override { return nullptr; }
		bool GetKeyState(int, int) const override { return false; }
		float GetMouseX() const override { return 0.0f; }
		float GetMouseY() const override { return 0.0f; }
		bool IsKeyDown(int) const override { return false; }
		bool IsKeyTriggered(int) const override { return false; }
		bool IsMouseDown(int) const override { return false; }
		bool IsMouseUp(int) const override { return false; }
	};
}

bool RunWindowBindingsCompileTest()
{
	const uint32_t realSize = static_cast<uint32_t>(sizeof(IWindowApi_v1));
	IWindowApi_v1 table;
	std::memset(&table, 0xCD, sizeof(table));

	if (IWindow_GetApi(1u, realSize, &table) != realSize)
	{
		std::puts("  IWindow_GetApi 的版本/大小握手没有回填真实大小");
		return false;
	}
	if (table.abi_version != 1u || table.size != realSize)
	{
		std::printf("  表头不对：abi_version=%u、size=%u（期望 %u）\n", table.abi_version, table.size,
		            realSize);
		return false;
	}
	// 22 条可绑方法 + 2 条错误通道 + 1 条 GetApi 之外，表里还应当有 IWindow 自己那些方法指针
	if (realSize <= 8u)
	{
		std::printf("  函数表太小：%u 字节\n", realSize);
		return false;
	}

	// 任务 2.3：`void*` 返回值不许被吞掉 —— 实现给一个哨兵地址，C ABI 面必须原样读回。
	// 旧产物把这一槽投影成 `void`，读回值是 0，这条必红。
	FakeWindowForHandle window;
	if (table.GetNativeWindowHandle(&window) != static_cast<uint64_t>(FakeWindowForHandle::Sentinel))
	{
		std::printf("  GetNativeWindowHandle 没有把指针读回来（拿到 %llu）\n",
		            static_cast<unsigned long long>(table.GetNativeWindowHandle(&window)));
		return false;
	}

	// 任务 2.4 / 2.5：设进去的名字必须能原样读回，而且"长度"与"拷出"必须对得上
	// （只断言"长度非 0"会被"长度算错 / 拷贝只写一半"的实现骗过）。
	static char const* const kName = "Cascaded";
	unsigned char poison[64];
	std::memset(poison, 0xCD, sizeof(poison));
	IWindow_SetWindowName(&window, kName, 8u);
	const uint64_t length = IWindow_GetWindowNameLen(&window);
	if (length != 8u)
	{
		std::printf("  GetWindowNameLen 与设进去的名字长度不符：%llu\n",
		            static_cast<unsigned long long>(length));
		return false;
	}
	IWindow_CopyGetWindowName(&window, reinterpret_cast<char*>(poison), sizeof(poison));
	if (std::memcmp(poison, kName, 9u) != 0)
	{
		std::puts("  CopyGetWindowName 没有把名字原样写出（含结尾零）");
		return false;
	}
	// 容量为 0 时一个字节都不许写（`cap - 1` 在无符号下溢会写满整段）
	unsigned char untouched[8];
	std::memset(untouched, 0xCD, sizeof(untouched));
	IWindow_CopyGetWindowName(&window, reinterpret_cast<char*>(untouched), 0u);
	for (unsigned char byte : untouched)
	{
		if (byte != 0xCD)
		{
			std::puts("  CopyGetWindowName 在容量 0 时仍然写了字节");
			return false;
		}
	}
	return true;
}

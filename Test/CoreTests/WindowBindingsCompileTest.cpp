// `IWindow` 的绑定产物能不能被真正编译并链接（`csharp-bindings-generator` 任务 2.10）。
//
// 这个文件 `#include` 了生成出来的 `IWindow.Bindings.cpp`：只要 CoreTests 能链接成功，就证明
// 那份产物**进了编译**（不是躺在磁盘上没人用）。运行期再验两条：
//   ① `IWindow_GetApi` 的版本/大小握手按约定工作；
//   ② 表里的方法条数与审计过的可绑方法数一致（23 条纯虚 − 1 条带理由排除 = 22）。

#include <CAWindow/WindowSystem.h>

#include "IWindow.Bindings.cpp"
// 任务 5.11 的交叉句柄判据要用**另一份产物**的释放入口（两张表合成一张之后靠接口标签区分，
// 见 design D12 乙）。这一个翻译单元里放两份产物没问题：它们的符号各自带接口前缀。
#include "IWindowSystem.Bindings.cpp"

#include <cstdio>
#include <cstring>
#include <memory>

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
		// 任务 5.3：`GetWindowSystem()` 现在会被绑成**借用句柄**，所以它必须回一个非空指针。
		// 这个地址只当"对象身份"用 —— 绑定期不解引用它（CoreTests 不加载 WindowSystem 模块）。
		cawindow::IWindowSystem* m_system = reinterpret_cast<cawindow::IWindowSystem*>(0x1234u);

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
		cawindow::IWindowSystem* GetWindowSystem() override { return m_system; }
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
	const uint32_t realSize = static_cast<uint32_t>(sizeof(IWindowApi_v2));
	IWindowApi_v2 table;
	std::memset(&table, 0xCD, sizeof(table));

	if (IWindow_GetApi(2u, realSize, &table) != realSize)
	{
		std::puts("  IWindow_GetApi 的版本/大小握手没有回填真实大小");
		return false;
	}
	if (table.abi_version != 2u || table.size != realSize)
	{
		std::printf("  表头不对：abi_version=%u、size=%u（期望 %u）\n", table.abi_version, table.size,
		            realSize);
		return false;
	}
	// 表大小**从审计过的条数算出来**，不从生成物里读（两侧都读生成物就是同源恒等）：
	// IWindow 有 23 条纯虚方法，全部可绑（任务 5.3 之后一条 `CA_BIND_SKIP` 都没有）；其中
	// `GetWindowName` 走两段式伴生、不占方法槽但占**两个**伴生槽 ⇒ 22 + 2 = 24 个方法侧槽位；
	// 再加表头的四个非方法槽（错误通道 2 + 句柄解析/释放 2）⇒ 28 个槽；本 change（任务 3.2）
	// 在末尾追加两个目标接口槽（ResolveTargetHandle / ReleaseTargetHandle）⇒ 30 个槽。
	// 每槽 8 字节，前面还有 abi_version/size 两个 4 字节字段 ⇒ 8 + 8 × 30 = 248 字节。
	const uint32_t expectedSlots = 4u + 22u + 2u + 2u;
	const uint32_t expectedSize = 8u + 8u * expectedSlots;
	if (realSize != expectedSize)
	{
		std::printf("  函数表大小不对：%u 字节（期望 %u = 8 + 8 × %u）\n", realSize, expectedSize,
		            expectedSlots);
		return false;
	}
	// 两个错误通道槽必须是能调用的真函数指针，而且按 self 分槽（任务 5.10）。
	// 两个假窗口是两个不同实例：一个失败不许把码串到另一个身上。
	FakeWindowForHandle window;
	FakeWindowForHandle otherWindow;
	if (table.LastErrorCode == nullptr || table.LastErrorText == nullptr)
	{
		std::puts("  函数表里的错误通道槽是空指针");
		return false;
	}
	if (table.LastErrorCode(&window) != 0 || table.LastErrorCode(&otherWindow) != 0)
	{
		std::puts("  新实例的错误码初始不是 0");
		return false;
	}
	// 造一次失败：解一个从来没发出去的句柄（代际 1、序号 9999）
	const uint64_t deadHandle = (1ull << 48) | 9999ull;
	if (IWindow_ResolveHandle(&window, deadHandle) != nullptr || table.LastErrorCode(&window) != 1001)
	{
		std::printf("  解失效句柄没有置 1001（读到 %d）\n", table.LastErrorCode(&window));
		return false;
	}
	if (table.LastErrorCode(&otherWindow) != 0)
	{
		std::puts("  一个实例的失败码串到了另一个实例上 —— 错误槽没有按 self 分");
		return false;
	}
	char errorText[64] = { 0 };
	table.LastErrorText(&window, errorText, sizeof(errorText));
	if (std::strcmp(errorText, "stale handle") != 0)
	{
		std::printf("  失败文本没有按 self 读回：\"%s\"\n", errorText);
		return false;
	}
	// 入口清零：紧接一次必成功的调用之后，同一实例的错误码必须回到 0（D8 不然会误抛）
	if (table.GetNativeWindowHandle(&window) != static_cast<uint64_t>(FakeWindowForHandle::Sentinel) ||
	    table.LastErrorCode(&window) != 0)
	{
		std::printf("  成功调用之后错误码没有清零（读到 %d）\n", table.LastErrorCode(&window));
		return false;
	}

	// 任务 2.3：`void*` 返回值不许被吞掉 —— 实现给一个哨兵地址，C ABI 面必须原样读回。
	// 旧产物把这一槽投影成 `void`，读回值是 0，这条必红。
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

	// ---- 任务 5.3：借用句柄 ----
	// `IWindow::GetWindowSystem()` 返回另一个可绑定接口的裸指针 ⇒ 投成借用句柄（kind 4）。
	// 判三条：① 解出的指针就是实现返回的那个；② 表项 kind == 4、且**不持有**强引用；
	// ③ 释放借用句柄只清槽（不销毁任何东西）。
	{
		const uint64_t borrowed = table.GetWindowSystem(&window);
		if (borrowed == 0u)
		{
			std::puts("  GetWindowSystem 没有产出借用句柄（0）");
			return false;
		}
		if (IWindow_ResolveHandle(&window, borrowed) != window.GetWindowSystem())
		{
			std::puts("  借用句柄解出来的指针与实现返回的不一致");
			return false;
		}
		const std::size_t slot = static_cast<std::size_t>((borrowed & ((1ull << 48) - 1ull)) - 1ull);
		if (slot >= IWindow_handles.size() || IWindow_handles[slot].kind != 4u ||
		    IWindow_handles[slot].strong != nullptr)
		{
			std::puts("  借用句柄的表项：kind 应为 4，且不许持有强引用");
			return false;
		}
		IWindow_ReleaseGetWindowSystem(&window, borrowed);
		if (IWindow_ResolveHandle(&window, borrowed) != nullptr)
		{
			std::puts("  借用句柄释放之后仍然能解出指针");
			return false;
		}
		// 从 5.5 的端到端测试挪过来的读数（那边不许再裸调 C ABI 了）：释放之后解析失败必须报 1001
		if (IWindow_LastErrorCode(&window) != 1001)
		{
			std::printf("  借用句柄释放后解析失败应报 1001，实际 %d\n", IWindow_LastErrorCode(&window));
			return false;
		}
	}

	// ---- 任务 5.11 ①：句柄发行者 ----
	// 拿 `IWindow` 发的借用句柄，交给 `IWindowSystem` 的释放入口。两张表合成一张、表项带接口
	// 标签之前，两个接口的句柄**数字完全相同**，这一下会把 IWindow 表里的活项清掉。
	{
		auto owner = std::make_shared<FakeWindowForHandle>();
		const uint64_t borrowed = IWindow_registerBorrowedHandle(owner.get(), owner->m_system, "");
		if (borrowed == 0u)
		{
			std::puts("  借用句柄登记失败（0）");
			return false;
		}
		IWindowSystem_ReleaseHandle(owner.get(), borrowed); // 错配：这个句柄不是 IWindowSystem 发的
		if (IWindow_ResolveHandle(owner.get(), borrowed) == nullptr)
		{
			std::puts("  错配的释放把另一个接口的活项清掉了（句柄表没有按接口标签区分）");
			return false;
		}
		if (IWindowSystem_LastErrorCode(owner.get()) != 1001)
		{
			std::printf("  错配的释放必须报 1001，实际 %d\n", IWindowSystem_LastErrorCode(owner.get()));
			return false;
		}
		IWindow_releaseHandle(owner.get(), borrowed);
	}

	// ---- 任务 5.11 ②：借用句柄的存活 ----
	// 见证者是**发行它的那个对象**：发行者死了，借用句柄必须解不出指针（不许解出一个悬垂地址）。
	{
		auto owner = std::make_shared<FakeWindowForHandle>();
		void* const borrowedTarget = owner->m_system;
		const uint64_t ownerHandle = IWindow_registerStrongHandle(owner.get(), std::shared_ptr<void>(owner), "");
		const uint64_t borrowed = IWindow_registerBorrowedHandle(owner.get(), borrowedTarget, "");
		if (ownerHandle == 0u || borrowed == 0u)
		{
			std::puts("  强句柄 / 借用句柄登记失败");
			return false;
		}
		if (IWindow_ResolveHandle(owner.get(), borrowed) != borrowedTarget)
		{
			std::puts("  发行者还活着时，借用句柄就该解得出指针");
			return false;
		}
		IWindow_releaseHandle(owner.get(), ownerHandle); // 先放掉表里的强引用
		owner.reset();                                   // 发行者真的死了
		if (IWindow_ResolveHandle(nullptr, borrowed) != nullptr)
		{
			std::puts("  发行者已销毁，借用句柄仍然解出了指针（悬垂）");
			return false;
		}
	}

	return true;
}

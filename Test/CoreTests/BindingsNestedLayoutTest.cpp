// 嵌套纯标量记录的镜像与按值搬运（`complete-csharp-window-system-bindings` 任务 1.1 / 3.1）。
//
// 四条读数：
//   ① 布局：镜像结构体与 C++ 侧的大小、外层字段偏移、嵌套字段偏移都一致；
//   ② 按值返回：`IBindRow22_GetMonitorInfo` 把两个矩形与 DPI 逐字段搬出来，非零读数不许丢；
//   ③ 默认比较运算符：`BindFloatRect` 的 defaulted `operator<=>` 不阻止生成（能编过即是证明，
//      运行期再断言排序真的按字段走）；
//   ④ 非法嵌套：`BindableShapes.RejectNested.h` 里的外层记录含一个带 `castl::string` 的内层，
//      生成器 MUST 非零退出并点名完整成员路径。
//
// 红态怎么构造：让生成器不产出 `IBindRow22.Bindings.cpp`（本文件就编不过），或让
// `BindMonitorInfoC` 少一个嵌套字段（偏移/读数断言失败），或让非法嵌套退出码变 0。

#include "BindableShapes.h"

#include "IBindRow22.Bindings.cpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <windows.h>

// `windows.h` 把 `GetMonitorInfo` 定义成 `GetMonitorInfoA/W` 宏；夹具接口上恰好有同名方法，
// 不撤销它的话这里的 override 会被改名（实测 C3668：method with override specifier did not override）。
#ifdef GetMonitorInfo
#undef GetMonitorInfo
#endif

namespace
{
	class FakeRow22 final : public ca_bind_fixture::IBindRow22
	{
	public:
		ca_bind_fixture::BindMonitorInfo GetMonitorInfo(int32_t monitorId) const override
		{
			m_lastMonitorId = monitorId;
			ca_bind_fixture::BindMonitorInfo info{};
			info.monitorRect.x = 10.5f;
			info.monitorRect.y = 20.5f;
			info.monitorRect.width = 1600.0f;
			info.monitorRect.height = 900.0f;
			info.workAreaRect.x = 15.5f;
			info.workAreaRect.y = 25.5f;
			info.workAreaRect.width = 1580.0f;
			info.workAreaRect.height = 860.0f;
			info.dpiScale = 1.5f;
			return info;
		}

		void SetMonitorInfo(ca_bind_fixture::BindMonitorInfo const& info) override
		{
			m_received = info;
			++m_receivedCount;
		}

		mutable int32_t m_lastMonitorId = -1;
		ca_bind_fixture::BindMonitorInfo m_received{};
		int m_receivedCount = 0;
	};

	bool SameRect(ca_bind_fixture::BindFloatRect const& left, ca_bind_fixture::BindFloatRect const& right)
	{
		return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
	}

	// 跑一次生成器，用它自己的退出码与输出证明非法嵌套被拒绝。CoreTests 是第 ① 层判据，
	// 但它要证明的正是"生成期拒绝"这件事，所以这里确实起一次 `CodeGen.exe`（与构建同一条命令形状）。
	bool RunNestedRejectProbe()
	{
		char exePath[MAX_PATH] = {};
		if (::GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0)
		{
			std::puts("  非法嵌套用例：取不到 CoreTests 自己的路径");
			return false;
		}
		const std::filesystem::path binDir = std::filesystem::path(exePath).parent_path();
		const std::filesystem::path tool = binDir / "CodeGen.exe";
		if (!std::filesystem::is_regular_file(tool))
		{
			std::printf("  非法嵌套用例：找不到 %s\n", tool.string().c_str());
			return false;
		}
		const std::filesystem::path sourceDir = CASCADED_SOURCE_DIR;
		const std::filesystem::path header = sourceDir / "Tools/CodeGen/BindableShapes.RejectNested.h";
		const std::filesystem::path audit = sourceDir / "Tools/CodeGen/check_bindable_audit.py";
		if (!std::filesystem::is_regular_file(header))
		{
			std::printf("  非法嵌套用例：找不到夹具头 %s\n", header.string().c_str());
			return false;
		}

		std::error_code ec;
		const std::filesystem::path outDir = std::filesystem::temp_directory_path() / "cascaded_nested_reject";
		std::filesystem::remove_all(outDir, ec);
		std::filesystem::create_directories(outDir, ec);
		const std::filesystem::path logPath = outDir / "codegen.log";
		// 走一个临时 .bat：`std::system` 直接拼一条以引号开头的长命令时，cmd 的引号剥离规则
		// 会让它报 "filename, directory name, or volume label syntax is incorrect"（实测）。
		// 批处理里一行一条命令，路径全是绝对路径，不受调用方 cwd 影响。
		const std::filesystem::path scriptPath = outDir / "run_codegen.bat";
		{
			std::ofstream script(scriptPath, std::ios::binary);
			script << "@echo off\r\n";
			script << "\"" << tool.string() << "\" \"" << header.string()
			       << "\" --emit-binding --out-dir \"" << outDir.string() << "\" --audit-script \""
			       << audit.string() << "\" \"-I" << (sourceDir / "CACore/header").string() << "\" \"-I"
			       << (sourceDir / "Tools/CodeGen/clang_shim").string() << "\" -xc++ > \"" << logPath.string()
			       << "\" 2>&1\r\n";
			script << "exit /b %ERRORLEVEL%\r\n";
		}
		const int exitCode = std::system(scriptPath.string().c_str());

		std::ifstream log(logPath, std::ios::binary);
		const std::string output((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
		log.close();
		std::filesystem::remove_all(outDir, ec);

		if (exitCode == 0)
		{
			std::printf("  非法嵌套用例：生成器退出码 0（应当非零），输出：\n%s\n", output.c_str());
			return false;
		}
		if (output.find("BindNestedBadOuter.inner.name") == std::string::npos)
		{
			std::printf("  非法嵌套用例：诊断里没有点名完整成员路径 BindNestedBadOuter.inner.name，输出：\n%s\n",
			            output.c_str());
			return false;
		}
		return true;
	}
}

bool RunBindingsNestedLayoutTest()
{
	// ① 布局：大小与每个字段（含嵌套字段）的偏移都由两侧真实 `sizeof` / `offsetof` 算出
	if (sizeof(BindMonitorInfoC) != sizeof(ca_bind_fixture::BindMonitorInfo))
	{
		std::printf("  BindMonitorInfoC 大小 %zu，C++ 侧 %zu\n", sizeof(BindMonitorInfoC),
		            sizeof(ca_bind_fixture::BindMonitorInfo));
		return false;
	}
	if (sizeof(BindFloatRectC) != sizeof(ca_bind_fixture::BindFloatRect))
	{
		std::printf("  BindFloatRectC 大小 %zu，C++ 侧 %zu\n", sizeof(BindFloatRectC),
		            sizeof(ca_bind_fixture::BindFloatRect));
		return false;
	}
	if (offsetof(BindMonitorInfoC, monitorRect) != offsetof(ca_bind_fixture::BindMonitorInfo, monitorRect) ||
	    offsetof(BindMonitorInfoC, workAreaRect) != offsetof(ca_bind_fixture::BindMonitorInfo, workAreaRect) ||
	    offsetof(BindMonitorInfoC, dpiScale) != offsetof(ca_bind_fixture::BindMonitorInfo, dpiScale))
	{
		std::puts("  BindMonitorInfoC 的外层字段偏移与 C++ 侧不一致");
		return false;
	}
	if (offsetof(BindFloatRectC, x) != offsetof(ca_bind_fixture::BindFloatRect, x) ||
	    offsetof(BindFloatRectC, y) != offsetof(ca_bind_fixture::BindFloatRect, y) ||
	    offsetof(BindFloatRectC, width) != offsetof(ca_bind_fixture::BindFloatRect, width) ||
	    offsetof(BindFloatRectC, height) != offsetof(ca_bind_fixture::BindFloatRect, height))
	{
		std::puts("  BindFloatRectC 的嵌套字段偏移与 C++ 侧不一致");
		return false;
	}

	// ② 按值返回：固定非零读数逐字段比对
	FakeRow22 object;
	const BindMonitorInfoC mirror = IBindRow22_GetMonitorInfo(&object, 7);
	if (object.m_lastMonitorId != 7)
	{
		std::printf("  GetMonitorInfo 收到的显示器编号是 %d，期望 7\n", object.m_lastMonitorId);
		return false;
	}
	if (mirror.monitorRect.x != 10.5f || mirror.monitorRect.y != 20.5f || mirror.monitorRect.width != 1600.0f ||
	    mirror.monitorRect.height != 900.0f)
	{
		std::printf("  m_MonitorRect 读数不符：(%g, %g, %g, %g)\n", mirror.monitorRect.x, mirror.monitorRect.y,
		            mirror.monitorRect.width, mirror.monitorRect.height);
		return false;
	}
	if (mirror.workAreaRect.x != 15.5f || mirror.workAreaRect.y != 25.5f ||
	    mirror.workAreaRect.width != 1580.0f || mirror.workAreaRect.height != 860.0f)
	{
		std::printf("  m_WorkAreaRect 读数不符：(%g, %g, %g, %g)\n", mirror.workAreaRect.x,
		            mirror.workAreaRect.y, mirror.workAreaRect.width, mirror.workAreaRect.height);
		return false;
	}
	if (mirror.dpiScale != 1.5f)
	{
		std::printf("  m_DPIScale 读数是 %g，期望 1.5\n", mirror.dpiScale);
		return false;
	}

	// ②' 按 const& 入参：镜像逐字段搬回 C++ 侧，两个矩形都不许串
	BindMonitorInfoC incoming{};
	incoming.monitorRect.x = 1.25f;
	incoming.monitorRect.y = 2.25f;
	incoming.monitorRect.width = 3.25f;
	incoming.monitorRect.height = 4.25f;
	incoming.workAreaRect.x = 5.25f;
	incoming.workAreaRect.y = 6.25f;
	incoming.workAreaRect.width = 7.25f;
	incoming.workAreaRect.height = 8.25f;
	incoming.dpiScale = 2.5f;
	IBindRow22_SetMonitorInfo(&object, &incoming);
	if (object.m_receivedCount != 1)
	{
		std::printf("  SetMonitorInfo 被调用 %d 次，期望 1 次\n", object.m_receivedCount);
		return false;
	}
	if (!SameRect(object.m_received.monitorRect, { 1.25f, 2.25f, 3.25f, 4.25f }) ||
	    !SameRect(object.m_received.workAreaRect, { 5.25f, 6.25f, 7.25f, 8.25f }) ||
	    object.m_received.dpiScale != 2.5f)
	{
		std::puts("  SetMonitorInfo 搬回 C++ 侧时字段错序或丢失");
		return false;
	}

	// ③ defaulted `operator<=>`：按声明的字段顺序比较（x → y → width → height）
	{
		const ca_bind_fixture::BindFloatRect low{ 1.0f, 2.0f, 3.0f, 4.0f };
		const ca_bind_fixture::BindFloatRect same{ 1.0f, 2.0f, 3.0f, 4.0f };
		const ca_bind_fixture::BindFloatRect high{ 1.0f, 2.0f, 3.0f, 5.0f };
		if (!((low <=> same) == 0) || !((low <=> high) < 0) || !((high <=> low) > 0))
		{
			std::puts("  BindFloatRect 的 defaulted operator<=> 没有按字段顺序比较");
			return false;
		}
	}

	// ④ 非法嵌套：非零退出并点名完整成员路径
	if (!RunNestedRejectProbe())
	{
		return false;
	}

	return true;
}

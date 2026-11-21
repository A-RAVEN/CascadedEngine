// FileLoader 三函数的失败显式化（failure-visibility-hygiene 1.2 / design D1+D2）。
//
// 改前：文件打不开时 `is_open()` 为假，直接跳过读写 —— 读静默返回空容器、写静默丢数据，调用方
// 拿不到任何信号。改后：打不开 / 读不足 / 写失败 / flush 失败一律抛 `cacore::file_error`，
// 消息里点名路径；空文件合法（返回空，不抛）。
//
// 用例全在 %TEMP% 下造真文件，结束清理。红态读数（缺陷仍在时）由 ①②④ 三行给出：
//   ① 缺失路径 LoadStringFile   —— 改前静默返回空
//   ② 缺失路径 LoadBinaryFile   —— 改前静默返回空
//   ④ 父目录不存在的 WriteBinaryFile —— 改前静默丢写
// ③（0 字节文件）与 ⑤（正常写读往返）是对照：改前改后都必须绿。
#include <FileLoader.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <process.h>
#include <string>
#include <system_error>

namespace
{
	bool Report(bool ok, char const* subject)
	{
		std::printf("  [FileLoader] %s：%s\n", ok ? "判据成立" : "判据不成立", subject);
		return ok;
	}

	// 跑一次「必须抛 file_error 且消息含路径」的判据。返回 true = 类型与消息都符合。
	// 改前实际读数会是「没有抛异常（静默返回/静默丢写）」。
	template <typename Call>
	bool ExpectFileError(Call&& call, std::string const& path, char const* subject)
	{
		try
		{
			call();
		}
		catch (cacore::file_error const& error)
		{
			const bool namesPath = std::strstr(error.what(), path.c_str()) != nullptr;
			std::printf("  [FileLoader] 抛出 file_error：%s（消息含路径=%s）\n", error.what(),
			            namesPath ? "是" : "否");
			return Report(namesPath, subject);
		}
		catch (std::exception const& error)
		{
			std::printf("  [FileLoader] 抛出了非 file_error 的异常：%s\n", error.what());
			return Report(false, subject);
		}
		catch (...)
		{
			std::printf("  [FileLoader] 抛出了非 std::exception 的异常\n");
			return Report(false, subject);
		}
		std::printf("  [FileLoader] 没有抛异常（改前行为：静默返回空 / 静默丢写）\n");
		return Report(false, subject);
	}
}

bool RunFileLoaderErrorTest()
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path dir = fs::temp_directory_path() /
	                     (std::string("cascaded_fileloader_error_") + std::to_string(_getpid()));
	fs::remove_all(dir, ec);
	fs::create_directories(dir, ec);
	if (ec)
	{
		std::printf("  [FileLoader] 无法创建临时目录 %s：%s\n", dir.string().c_str(), ec.message().c_str());
		return false;
	}

	bool ok = true;
	const std::string missingText = (dir / "missing_text.txt").string();
	const std::string missingBinary = (dir / "missing_binary.bin").string();
	const std::string emptyBinary = (dir / "empty.bin").string();
	const std::string missingWriteDir = (dir / "no_such_subdir" / "output.bin").string();

	// ① 缺失路径读文本（改前红：静默返回空）。
	ok = ExpectFileError([&] { (void)cacore::LoadStringFile(missingText); }, missingText,
	                     "缺失路径 LoadStringFile 抛 file_error 且消息含路径") && ok;

	// ② 缺失路径读二进制（改前红：静默返回空）。
	ok = ExpectFileError([&] { (void)cacore::LoadBinaryFile(missingBinary); }, missingBinary,
	                     "缺失路径 LoadBinaryFile 抛 file_error 且消息含路径") && ok;

	// ③ 0 字节文件合法（对照：改前改后都必须绿）。
	{
		bool created = false;
		if (std::FILE* file = std::fopen(emptyBinary.c_str(), "wb"))
		{
			std::fclose(file);
			created = true;
		}
		bool threw = false;
		castl::vector<uint8_t> bytes;
		try
		{
			bytes = cacore::LoadBinaryFile(emptyBinary);
		}
		catch (std::exception const& error)
		{
			threw = true;
			std::printf("  [FileLoader] 空文件意外抛出：%s\n", error.what());
		}
		std::printf("  [FileLoader] 0 字节文件：创建=%s 抛出=%s 返回字节数=%zu\n", created ? "是" : "否",
		            threw ? "是" : "否", bytes.size());
		ok = Report(created && !threw && bytes.empty(), "0 字节文件 LoadBinaryFile 返回空且不抛") && ok;
	}

	// ④ 父目录不存在的写（改前红：静默丢写）。
	{
		const char payload[] = "file-loader-error-test";
		ok = ExpectFileError([&] { cacore::WriteBinaryFile(missingWriteDir, payload, sizeof(payload)); },
		                     missingWriteDir, "父目录不存在的 WriteBinaryFile 抛 file_error 且消息含路径") && ok;
		const bool written = fs::exists(missingWriteDir, ec);
		std::printf("  [FileLoader] 缺失目录写之后目标文件存在=%s（改前为否，静默丢写）\n", written ? "是" : "否");
	}

	// ⑤ 正常写入 / 读回往返（对照：改后不得把成功路径也当成失败）。
	{
		const std::string roundtrip = (dir / "roundtrip.bin").string();
		const char payload[] = "abc";
		bool pass = true;
		try
		{
			cacore::WriteBinaryFile(roundtrip, payload, 3);
		}
		catch (std::exception const& error)
		{
			pass = false;
			std::printf("  [FileLoader] 正常写入意外抛出：%s\n", error.what());
		}
		try
		{
			const castl::vector<uint8_t> bytes = cacore::LoadBinaryFile(roundtrip);
			pass = pass && bytes.size() == 3 && std::memcmp(bytes.data(), payload, 3) == 0;
		}
		catch (std::exception const& error)
		{
			pass = false;
			std::printf("  [FileLoader] 正常读回意外抛出：%s\n", error.what());
		}
		ok = Report(pass, "正常写入后读回的字节一致（成功路径不受影响）") && ok;
	}

	fs::remove_all(dir, ec);
	return ok;
}

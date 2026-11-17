#pragma once
#include <stdint.h>
#include <stdexcept>
#include <string>
#include <CASTL/CASharedPtr.h>
#include <ThreadManager.h>

namespace ca_io
{
	// 类型化 I/O 失败（design D1）：消息点名来源路径与原因。
	// 派生 `std::runtime_error`，声明放在接口头而不是实现模块里 —— 实现侧（IOManager_FS）、
	// 资源层与测试经同一个头包含，异常类型的身份才是同一个（design Risk「DLL 边界异常类型识别」）。
	class io_error : public std::runtime_error
	{
	public:
		explicit io_error(std::string const& message)
			: std::runtime_error(message)
		{
		}
	};

	class IOBatch
	{
	public:
		// 返回本次读是否被来源接受（越界、来源错误 ⇒ false）。来源自己报告，调用方不得丢弃。
		virtual bool Read(uint64_t readSize, void* destination) = 0;
		virtual void Seek(uint64_t offset) = 0;
		virtual void SubmitAndWait() = 0;
		virtual uint32_t SubmitCount() const = 0;
		// 来源路径：上层抛 `io_error` 时点名文件（design D5 经它从批上取路径）。
		virtual castl::string const& SourcePath() const = 0;
		virtual ~IOBatch() = default;
		template <typename T>
		bool Read(T& destination)
		{
			return Read(sizeof(T), &destination);
		}
	};

	class WBatch
	{
	public:
		// 返回本次写是否被来源接受。写目标打不开之类的失败在 `SubmitAndWait` 抛 `io_error`。
		virtual bool Write(uint64_t readSize, void const* destination) = 0;
		virtual void Seek(uint64_t offset) = 0;
		virtual void SubmitAndWait() = 0;
		virtual uint32_t SubmitCount() const = 0;
		// 目标路径：上层抛 `io_error` 时点名文件。
		virtual castl::string const& SourcePath() const = 0;
		virtual ~WBatch() = default;
		template <typename T>
		bool Write(T const& destination)
		{
			return Write(sizeof(T), &destination);
		}
	};

	class IOManager
	{
	public:
		virtual void Initialize(thread_management::CThreadManager* threadManager) = 0;
		virtual castl::shared_ptr<IOBatch> Batch(castl::string_view) = 0;
		virtual castl::shared_ptr<WBatch> WriteBatch(castl::string_view) = 0;
		virtual ~IOManager() = default;
	};
}

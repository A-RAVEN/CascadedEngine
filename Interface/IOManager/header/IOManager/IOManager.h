#pragma once
#include <stdint.h>
#include <CASTL/CASharedPtr.h>
#include <ThreadManager.h>

namespace ca_io
{
	class IOBatch
	{
	public:
		virtual void Read(uint64_t readSize, void* destination) = 0;
		virtual void Seek(uint64_t offset) = 0;
		virtual void SubmitAndWait() = 0;
		virtual uint32_t SubmitCount() const = 0;
		virtual ~IOBatch() = default;
		template <typename T>
		void Read(T& destination)
		{
			Read(sizeof(T), &destination);
		}
	};

	class WBatch
	{
	public:
		virtual void Write(uint64_t readSize, void const* destination) = 0;
		virtual void Seek(uint64_t offset) = 0;
		virtual void SubmitAndWait() = 0;
		virtual uint32_t SubmitCount() const = 0;
		virtual ~WBatch() = default;
		template <typename T>
		void Write(T const& destination)
		{
			Write(sizeof(T), &destination);
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
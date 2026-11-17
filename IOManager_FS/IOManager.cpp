#include <IOManager/IOManager.h>
#include <CASTL/CASet.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAList.h>
#include <ThreadManager.h>
#include <fstream>
//#include <LibraryExportCommon.h>
#define CA_IMPLEMENT_MODULE 1
#include <CACore/CAModuleImplementation.h>
#include <filesystem>
#include <CACore/CASharedDic.h>

namespace ca_io
{
	class IOManagerImpl;

	// `io_error` 的消息统一形状：「<动作>：<"路径">（<原因>）」。路径与原因都点名，
	// 上层（资源层、后端、测试）不用再去猜是哪个文件、为什么失败（design D1/D5）。
	static castl::string MakeIoErrorMessage(char const* action, castl::string_view path, castl::string_view reason)
	{
		castl::string message(action);
		message += "：\"";
		message.append(path.data(), path.size());
		message += "\"";
		if (!reason.empty())
		{
			message += "（";
			message.append(reason.data(), reason.size());
			message += "）";
		}
		return message;
	}

	// 探测源文件的尺寸（design D4）。路径不存在、路径是目录、没权限……一律转成 `io_error` 并点名路径：
	// 裸调 `std::filesystem::file_size` 抛的是未类型化的 `std::filesystem::filesystem_error`，
	// 调用方在另一侧认不出它，只能当"未知异常"。
	static uint64_t ProbeSourceFileSize(castl::string_view filePath)
	{
		std::filesystem::path const path(filePath);
		std::error_code probeError;
		if (std::filesystem::is_directory(path, probeError))
		{
			throw io_error(MakeIoErrorMessage("批创建期探测源文件失败", filePath, "路径是目录，不是可读的文件"));
		}
		const uint64_t size = static_cast<uint64_t>(std::filesystem::file_size(path, probeError));
		if (probeError)
		{
			throw io_error(MakeIoErrorMessage("批创建期探测源文件失败", filePath, probeError.message()));
		}
		return size;
	}
	
	struct IORead
	{
		uint64_t offset;
		uint64_t size;
		void* destination;
	};

	struct IORange
	{
		uint64_t offset;
		uint64_t size;

		IORange(IORead const& read):
			offset(read.offset),
			size(read.size)
		{
		}

		uint64_t GetEnd() const
		{
			return offset + size;
		}
		bool CanCombine(IORange const& other) const
		{
			if (offset > other.GetEnd() || GetEnd() < other.offset)
				return false;
			return true;
		}
		void Expand(IORange const& other)
		{
			uint64_t maxPos = castl::max(offset + size, other.offset + other.size);
			offset = castl::min(offset, other.offset);
			size = maxPos - offset;
		}
	};

	class IOChunkState
	{
	public:
		IOChunkState(IORead const& readRange) : m_ChunkRange(readRange), m_Reads({ readRange })
		{
		}
		bool TryCombineNext(IOChunkState const& nextChunk)
		{
			if (nextChunk.m_ChunkRange.offset <= (m_ChunkRange.offset + m_ChunkRange.size))
			{
				m_ChunkRange.Expand(nextChunk.m_ChunkRange);
				size_t start = m_Reads.size();
				m_Reads.resize(start + nextChunk.m_Reads.size());
				for (size_t id = 0; id < nextChunk.m_Reads.size(); ++id)
				{
					m_Reads[start + id] = nextChunk.m_Reads[id];
				}
				return true;
			}
			return false;
		}
		bool TryAddRead(IORead const& readRange)
		{
			if (m_ChunkRange.CanCombine(readRange))
			{
				m_ChunkRange.Expand(readRange);
				m_Reads.push_back(readRange);
				return true;
			}
			return false;
		}
		IORange m_ChunkRange;
		castl::vector<IORead> m_Reads;
	};

	class BatchCache
	{
	public:
		BatchCache(castl::string_view filePath): m_FilePath(filePath)
		{
			m_NextChunk = m_Chunks.end();
		}
		uint64_t GetCurrentPos() const
		{
			return m_CurrrentPos;
		}
		void AppendRange(size_t rwSize, void* rwDest)
		{
			if (rwSize == 0u)
				return;

			IORead newRange{ m_CurrrentPos, rwSize, rwDest };
			if (m_NextChunk == m_Chunks.end() || !m_NextChunk->TryAddRead(newRange))
			{
				m_NextChunk = m_Chunks.insert(m_NextChunk, IOChunkState(newRange));
			}

			if (m_NextChunk != m_Chunks.begin())
			{
				auto prevChunk = m_NextChunk;
				--prevChunk;
				if (prevChunk->TryCombineNext(*m_NextChunk))
				{
					m_Chunks.erase(m_NextChunk);
					m_NextChunk = prevChunk;
				}
			}
			{
				auto newNextChunk = m_NextChunk;
				++newNextChunk;
				if (newNextChunk != m_Chunks.end() && m_NextChunk->TryCombineNext(*newNextChunk))
				{
					m_Chunks.erase(newNextChunk);
				}
			}
			m_CurrrentPos += rwSize;
		}
		void Seek(uint64_t offset)
		{
			m_CurrrentPos = offset;
			if (m_Chunks.empty())
			{
				m_NextChunk = m_Chunks.end();
				return;
			}
			for (m_NextChunk = m_Chunks.begin(); m_NextChunk != m_Chunks.end(); ++m_NextChunk)
			{
				if (m_NextChunk->m_ChunkRange.GetEnd() >= offset)
				{
					break;
				}
			}
		}
		bool IsEmpty() const
		{
			return m_Chunks.empty();
		}
		void Clear()
		{
			m_Chunks.clear();
			m_NextChunk = m_Chunks.end();
		}
		castl::list<IOChunkState>::iterator m_NextChunk;
		castl::list<IOChunkState> m_Chunks;
		uint64_t m_CurrrentPos = 0;
		cacore::PathHash m_FilePath;
	};

	class IOBatchImpl : public IOBatch
	{
	public:

		IOBatchImpl(IOManagerImpl* owningManager, castl::string_view filePath, uint64_t fileSize);
		virtual bool Read(uint64_t readSize, void* destination) override;
		virtual void Seek(uint64_t offset) override;
		virtual void SubmitAndWait() override;
		virtual uint32_t SubmitCount() const override;
		virtual castl::string const& SourcePath() const override { return m_SourcePath; }

		BatchCache m_Batchs;
		uint64_t m_SubmitCount = 0;
		uint64_t m_FileSize = 0;
		IOManagerImpl* m_OwningManager;
		castl::string m_SourcePath;
	};

	class WriteBatchImpl : public WBatch
	{
	public:

		WriteBatchImpl(IOManagerImpl* owningManager, castl::string_view filePath) : m_OwningManager(owningManager), m_Batchs(filePath), m_SourcePath(filePath) {}
		virtual bool Write(uint64_t readSize, void const* destination) override { m_Batchs.AppendRange(readSize, (void*)destination); return true; }
		virtual void Seek(uint64_t offset) override { m_Batchs.Seek(offset); }
		virtual void SubmitAndWait() override;
		virtual uint32_t SubmitCount() const override { return m_SubmitCount; }
		virtual castl::string const& SourcePath() const override { return m_SourcePath; }

		BatchCache m_Batchs;
		uint64_t m_SubmitCount = 0;
		IOManagerImpl* m_OwningManager;
		castl::string m_SourcePath;
	};

	class OStreamContext
	{
	public:
		OStreamContext(OStreamContext&& other) noexcept
		{
			castl::lock_guard(other.m_Mutex);
			m_Stream = std::move(other.m_Stream);
		}
		OStreamContext(castl::string const& path) : m_Stream(path, std::ios::out | std::ios::binary)
		{
		}
		~OStreamContext()
		{
			if (m_Stream.is_open())
				m_Stream.close();
		}
		castl::mutex m_Mutex;
		castl::ofstream m_Stream;
	private:
	};

	class OStreamLock
	{
	public:
		OStreamLock(OStreamLock const&) = delete;
		OStreamLock& operator=(OStreamLock const&) = delete;
		OStreamLock(OStreamContext& context) : m_Context(context), m_Lock(context.m_Mutex)
		{
		}
		castl::ofstream& GetStream()
		{
			return m_Context.m_Stream;
		}
		castl::ofstream* operator->()
		{
			return &m_Context.m_Stream;
		}
	private:
		castl::lock_guard<castl::mutex> m_Lock;
		OStreamContext& m_Context;
	};

	class IOManagerImpl : public IOManager
	{
	public:

		void Init(cacore::IModuleManager* pModuleManager)
		{
			pThreadManager = pModuleManager->GetInstance<thread_management::CThreadManager>();
		}

		virtual void Initialize(thread_management::CThreadManager* threadManager) override
		{
			pThreadManager = threadManager;
		}

		virtual castl::shared_ptr<IOBatch> Batch(castl::string_view filePath) override
		{
			// design D4：尺寸在批创建时探测一次，探测结果交给批（`Read` 的越界判断用它）；
			// 探测失败在这里就点名路径抛 `io_error`，不拖到 Submit。
			const uint64_t fileSize = ProbeSourceFileSize(filePath);
			return castl::shared_ptr<IOBatch>(new IOBatchImpl(this, filePath, fileSize));
		}

		virtual castl::shared_ptr<WBatch> WriteBatch(castl::string_view filePath) override
		{
			return castl::shared_ptr<WBatch>(new WriteBatchImpl(this, filePath));
		}

		OStreamLock GetOStream(cacore::PathHash const& filePath)
		{
			auto result = m_OStreamCache.get_or_create(filePath, [&](cacore::PathHash const& key)
			{
				return OStreamContext(key.string());
			});
			return OStreamLock(result->second);
		}

		thread_management::CThreadManager* pThreadManager;
		castl::shared_dic<cacore::PathHash, OStreamContext> m_OStreamCache;
	};

	IOBatchImpl::IOBatchImpl(IOManagerImpl* owningManager, castl::string_view filePath, uint64_t fileSize)
		: m_Batchs(filePath), m_FileSize(fileSize), m_OwningManager(owningManager), m_SourcePath(filePath)
	{
	}

	bool IOBatchImpl::Read(uint64_t readSize, void* destination)
	{
		// design D3：越界返回 false 且**不推进**批内读位置 —— 目标内存保持原样，
		// 上层的读对象据此把自己的失败位置起来。旧实现是静默 `return`，调用方看不见。
		if (m_Batchs.GetCurrentPos() + readSize > m_FileSize)
			return false;
		m_Batchs.AppendRange(readSize, destination);
		return true;
	}
	void IOBatchImpl::Seek(uint64_t offset)
	{
		m_Batchs.Seek(offset);
	}

	uint32_t IOBatchImpl::SubmitCount() const
	{
		return m_SubmitCount;
	}

	void IOBatchImpl::SubmitAndWait()
	{
		if (m_Batchs.IsEmpty())
			return;
		auto scheduler = m_OwningManager->pThreadManager->NewScheduler();
		// design D2：任务体内**不抛异常**，失败按值收进 `failureReason`；`WaitAll()` 之后在调用线程统一抛。
		// 失败时不 `Clear()`：没被填充的读还在队里，清掉等于把"哪些读没做成"也一起丢了。
		castl::string failureReason;
		scheduler->NewTask()
			->Name("IOBatch")
			->Functor([&]()
				{
					std::ifstream file_src(m_Batchs.m_FilePath.string(), std::ios::in | std::ios::binary);

					uint64_t maxChunkSize = 0;
					for (auto& chunkState : m_Batchs.m_Chunks)
					{
						maxChunkSize = castl::max(maxChunkSize, chunkState.m_ChunkRange.size);
					}

					castl::vector<uint8_t> stageBuffer;
					stageBuffer.resize(maxChunkSize);
					if (!file_src.is_open())
					{
						// 设计上的 TOCTOU 双保险：创建批之后源文件被删/改权限，这里逮到，
						// 旧实现是"什么都没读、也不报错"。
						failureReason = "打不开源文件";
						return;
					}
					file_src.seekg(0, std::ios::beg);
					for (auto& chunkState : m_Batchs.m_Chunks)
					{
						file_src.seekg(chunkState.m_ChunkRange.offset, std::ios::beg);
						file_src.read(reinterpret_cast<char*>(stageBuffer.data()), chunkState.m_ChunkRange.size);
						// design D2 的读写中途检查：`gcount()` 不等于这一块的请求尺寸就是短读
						// （创建批之后源文件被截短的 TOCTOU）。旧实现会把 stage 里的残渣静默抄进目标内存。
						const uint64_t bytesRead = static_cast<uint64_t>(file_src.gcount());
						if (bytesRead != chunkState.m_ChunkRange.size)
						{
							failureReason = "源文件读不足（偏移 ";
							failureReason += std::to_string(chunkState.m_ChunkRange.offset);
							failureReason += "、请求 ";
							failureReason += std::to_string(chunkState.m_ChunkRange.size);
							failureReason += " 字节、实际读到 ";
							failureReason += std::to_string(bytesRead);
							failureReason += " 字节；创建批之后源文件被截短？）";
							return;
						}
						for (auto& read : chunkState.m_Reads)
						{
							size_t localOffset = read.offset - chunkState.m_ChunkRange.offset;
							memcpy(read.destination, stageBuffer.data() + localOffset, read.size);
						}
					}
					file_src.close();
					m_Batchs.Clear();
				});
		scheduler->WaitAll();
		++m_SubmitCount;
		if (!failureReason.empty())
		{
			throw io_error(MakeIoErrorMessage("批读提交失败", m_SourcePath, failureReason));
		}
	}

	void WriteBatchImpl::SubmitAndWait()
	{
		if (m_Batchs.IsEmpty())
			return;
		auto scheduler = m_OwningManager->pThreadManager->NewScheduler();
		// design D2/D4 的写侧：目标打不开、`write()` / `flush()` 之后流状态失败，都在任务体内按值收下，
		// `WaitAll()` 之后在调用线程抛 `io_error` 点名路径。旧实现两种失败都是静默丢弃已排队的写。
		castl::string failureReason;
		scheduler->NewTask()
			->Name("IOBatch")
			->Functor([&]()
				{
					auto lockedOStream = m_OwningManager->GetOStream(m_Batchs.m_FilePath);
					auto& file_src = lockedOStream.GetStream();

					uint64_t maxChunkSize = 0;
					for (auto& chunkState : m_Batchs.m_Chunks)
					{
						maxChunkSize = castl::max(maxChunkSize, chunkState.m_ChunkRange.size);
					}

					castl::vector<uint8_t> stageBuffer;
					stageBuffer.resize(maxChunkSize);
					if (!file_src.is_open())
					{
						// 父目录不存在、路径是目录、没有写权限：`ofstream` 打开就失败。
						failureReason = "打不开写目标";
						return;
					}
					file_src.seekp(0, std::ios::beg);
					for (auto& chunkState : m_Batchs.m_Chunks)
					{
						file_src.seekp(chunkState.m_ChunkRange.offset, std::ios::beg);
						for (auto& read : chunkState.m_Reads)
						{
							size_t localOffset = read.offset - chunkState.m_ChunkRange.offset;
							memcpy(stageBuffer.data() + localOffset, read.destination, read.size);
						}
						file_src.write(reinterpret_cast<char*>(stageBuffer.data()), chunkState.m_ChunkRange.size);
						if (!file_src.good())
						{
							failureReason = "写失败（偏移 ";
							failureReason += std::to_string(chunkState.m_ChunkRange.offset);
							failureReason += "、请求 ";
							failureReason += std::to_string(chunkState.m_ChunkRange.size);
							failureReason += " 字节之后流状态变坏）";
							return;
						}
					}
					file_src.flush();
					if (!file_src.good())
					{
						failureReason = "flush 之后流状态变坏";
						return;
					}
					m_Batchs.Clear();
				});
		scheduler->WaitAll();
		++m_SubmitCount;
		if (!failureReason.empty())
		{
			throw io_error(MakeIoErrorMessage("批写提交失败", m_SourcePath, failureReason));
		}
	}

	//CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(IOManager, IOManagerImpl)
}
CA_MODULE_INSTANCE(ca_io::IOManager, ca_io::IOManagerImpl, IOManager_FS);


#include <IOManager/IOManager.h>
#include <CASTL/CASet.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAList.h>
#include <ThreadManager.h>
#include <fstream>
#include <LibraryExportCommon.h>
#include <filesystem>
#include <CACore/CASharedDic.h>

namespace ca_io
{
	class IOManagerImpl;
	
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

		IOBatchImpl(IOManagerImpl* owningManager, castl::string_view filePath);
		virtual void Read(uint64_t readSize, void* destination) override;
		virtual void Seek(uint64_t offset) override;
		virtual void SubmitAndWait() override;
		virtual uint32_t SubmitCount() const override;

		BatchCache m_Batchs;
		uint64_t m_SubmitCount = 0;
		uint64_t m_FileSize = 0;
		IOManagerImpl* m_OwningManager;
	};

	class WriteBatchImpl : public WBatch
	{
	public:

		WriteBatchImpl(IOManagerImpl* owningManager, castl::string_view filePath) : m_OwningManager(owningManager), m_Batchs(filePath) {}
		virtual void Write(uint64_t readSize, void const* destination) override { m_Batchs.AppendRange(readSize, (void*)destination); }
		virtual void Seek(uint64_t offset) override { m_Batchs.Seek(offset); }
		virtual void SubmitAndWait() override;
		virtual uint32_t SubmitCount() const override { return m_SubmitCount; }

		BatchCache m_Batchs;
		uint64_t m_SubmitCount = 0;
		IOManagerImpl* m_OwningManager;
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
		virtual void Initialize(thread_management::CThreadManager* threadManager) override
		{
			pThreadManager = threadManager;
		}

		virtual castl::shared_ptr<IOBatch> Batch(castl::string_view filePath) override
		{
			return castl::shared_ptr<IOBatch>(new IOBatchImpl(this, filePath));
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

	IOBatchImpl::IOBatchImpl(IOManagerImpl* owningManager, castl::string_view filePath) : m_OwningManager(owningManager), m_Batchs(filePath)
	{
		m_FileSize = std::filesystem::file_size(filePath);
	}

	void IOBatchImpl::Read(uint64_t readSize, void* destination)
	{
		if (m_Batchs.GetCurrentPos() + readSize > m_FileSize)
			return;
		m_Batchs.AppendRange(readSize, destination);
		
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
					if (file_src.is_open())
					{
						file_src.seekg(0, std::ios::beg);
						for (auto& chunkState : m_Batchs.m_Chunks)
						{
							file_src.seekg(chunkState.m_ChunkRange.offset, std::ios::beg);
							file_src.read(reinterpret_cast<char*>(stageBuffer.data()), chunkState.m_ChunkRange.size);
							for (auto& read : chunkState.m_Reads)
							{
								size_t localOffset = read.offset - chunkState.m_ChunkRange.offset;
								memcpy(read.destination, stageBuffer.data() + localOffset, read.size);
							}
						}
						file_src.close();
					}
					m_Batchs.Clear();
				});
		scheduler->WaitAll();
		++m_SubmitCount;
	}

	void WriteBatchImpl::SubmitAndWait()
	{
		if (m_Batchs.IsEmpty())
			return;
		auto scheduler = m_OwningManager->pThreadManager->NewScheduler();
		scheduler->NewTask()
			->Name("IOBatch")
			->Functor([&]()
				{
					auto lockedOStream = m_OwningManager->GetOStream(m_Batchs.m_FilePath);
					auto& file_src = lockedOStream.GetStream();
					//std::ofstream file_src(castl::to_std(m_Batchs.m_FilePath), std::ios::out | std::ios::binary);

					uint64_t maxChunkSize = 0;
					for (auto& chunkState : m_Batchs.m_Chunks)
					{
						maxChunkSize = castl::max(maxChunkSize, chunkState.m_ChunkRange.size);
					}

					castl::vector<uint8_t> stageBuffer;
					stageBuffer.resize(maxChunkSize);
					if (file_src.is_open())
					{
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
						}
					}
					file_src.flush();
					m_Batchs.Clear();
				});
		scheduler->WaitAll();
		++m_SubmitCount;
	}


	CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(IOManager, IOManagerImpl)
}


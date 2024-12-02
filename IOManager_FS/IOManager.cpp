#include <IOManager/IOManager.h>
#include <CASTL/CASet.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAList.h>
#include <ThreadManager.h>
#include <fstream>
#include <LibraryExportCommon.h>
#include <filesystem>

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

	class IOBatchImpl : public IOBatch
	{
	public:

		IOBatchImpl(IOManagerImpl* owningManager, castl::string_view filePath);
		virtual void Read(uint64_t readSize, void* destination) override;
		virtual void Seek(uint64_t offset) override;
		virtual void SubmitAndWait() override;
		virtual uint32_t SubmitCount() const override;
		castl::list<IOChunkState>::iterator m_NextChunk;
		castl::list<IOChunkState> m_Chunks;
		uint64_t m_CurrrentPos = 0;
		uint64_t m_FileSize = 0;
		uint64_t m_SubmitCount = 0;

		IOManagerImpl* m_OwningManager;
		castl::string m_FilePath;
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
		thread_management::CThreadManager* pThreadManager;
	};

	IOBatchImpl::IOBatchImpl(IOManagerImpl* owningManager, castl::string_view filePath) : m_OwningManager(owningManager), m_FilePath(filePath)
	{
		m_FileSize = std::filesystem::file_size(filePath);
		m_NextChunk = m_Chunks.end();
	}

	void IOBatchImpl::Read(uint64_t readSize, void* destination)
	{
		if (readSize == 0u)
			return;
		if (m_CurrrentPos + readSize > m_FileSize)
			return;
		IORead newRange{ m_CurrrentPos, readSize, destination };
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
		m_CurrrentPos += readSize;
	}
	void IOBatchImpl::Seek(uint64_t offset)
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

	uint32_t IOBatchImpl::SubmitCount() const
	{
		return m_SubmitCount;
	}

	void IOBatchImpl::SubmitAndWait()
	{
		if (m_Chunks.empty())
			return;
		auto scheduler = m_OwningManager->pThreadManager->NewScheduler();
		scheduler->NewTask()
			->Name("IOBatch")
			->Functor([&]()
				{
					std::ifstream file_src(castl::to_std(m_FilePath), std::ios::in | std::ios::binary);

					uint64_t maxChunkSize = 0;
					for (auto& chunkState : m_Chunks)
					{
						maxChunkSize = castl::max(maxChunkSize, chunkState.m_ChunkRange.size);
					}

					castl::vector<uint8_t> stageBuffer;
					stageBuffer.resize(maxChunkSize);
					if (file_src.is_open())
					{
						/*file_src.seekg(0, std::ios::end);
						m_FileSize = file_src.tellg();*/
						file_src.seekg(0, std::ios::beg);

						for (auto& chunkState : m_Chunks)
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
					m_Chunks.clear();
					m_NextChunk = m_Chunks.end();
				});
		scheduler->WaitAll();
		++m_SubmitCount;
	}


	CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(IOManager, IOManagerImpl)
}


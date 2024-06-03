#pragma once
#include <mutex>
#include <deque>
#include <DebugUtils.h>
#include <ThreadManager.h>
#include <EASTL/atomic.h>
#include <CASTL/CADeque.h>
#include <CASTL/CAUnorderedSet.h>
#include <CASTL/CAMutex.h>
#include "RenderBackendSettings.h"
#include "VulkanApplicationSubobjectBase.h"

namespace graphics_backend
{
	class ITickingResourceUpdator;

	class BaseTickingUpdateResource : public VKAppSubObjectBaseNoCopy
	{
	public:
		BaseTickingUpdateResource(CVulkanApplication& app);
		void SetOwningUpdator(ITickingResourceUpdator* owningUpdator);
		ITickingResourceUpdator* GetOwningUpdator() const;
		void ResetResource();
		bool IsValid() const;
		bool IsDirty() const;
		bool UploadingDone() const;
		virtual void TickUpload() = 0;
		void MarkDirtyThisFrame();
	protected:
		void MarkUploadingDoneThisFrame();
	private:
		bool b_Dirty = false;
		ITickingResourceUpdator* m_OwningUpdator = nullptr;
		FrameType m_SubmitFrame = INVALID_FRAMEID;
	};

	class ITickingResourceUpdator
	{
	public:
		virtual void EnqueueTickingResource(BaseTickingUpdateResource* resource) = 0;
	};

	template<typename T>
	class TTickingUpdateResourcePool :  public ITickingResourceUpdator
	{
	public:
		TTickingUpdateResourcePool() = delete;
		TTickingUpdateResourcePool(TTickingUpdateResourcePool const& other) = delete;
		TTickingUpdateResourcePool& operator=(TTickingUpdateResourcePool const&) = delete;
		TTickingUpdateResourcePool(TTickingUpdateResourcePool&& other) = delete;
		TTickingUpdateResourcePool& operator=(TTickingUpdateResourcePool&&) = delete;

		TTickingUpdateResourcePool(CVulkanApplication& owner) :
			m_Owner(owner)
		{
		}

		virtual ~TTickingUpdateResourcePool()
		{
			CA_ASSERT(IsEmpty(), (castl::string{"Vulkan Application Pointer Pool Is Not Released Before Destruct: "} + CA_CLASS_NAME(T)).c_str());
		}

		template<typename...TArgs>
		T* Alloc(TArgs&&...Args)
		{
			castl::lock_guard<castl::recursive_mutex> lockGuard(m_Mutex);
			T* result = nullptr;
			if (m_EmptySpaces.empty())
			{
				m_Pool.emplace_back(m_Owner);
				result = &m_Pool.back();
			}
			else
			{
				result = m_EmptySpaces.front();
				m_EmptySpaces.pop_front();
			}
			result->SetOwningUpdator(this);
			result->Initialize(castl::forward<TArgs>(Args)...);
			result->MarkDirtyThisFrame();
			return result;
		}

		void Release(T* releaseObj)
		{
			CA_ASSERT(releaseObj != nullptr, (castl::string{"Try Release nullptr: "} + CA_CLASS_NAME(T)).c_str());
			castl::lock_guard<castl::recursive_mutex> lockGuard(m_Mutex);
			//releaseObj->Release();
			releaseObj->ResetResource();
			m_EmptySpaces.push_back(releaseObj);
		}

		void ReleaseAll()
		{
			castl::lock_guard<castl::recursive_mutex> lockGuard(m_Mutex);

			castl::unordered_set<T*> emptySet;
			for (T* emptyPtr : m_EmptySpaces)
			{
				emptySet.insert(emptyPtr);
			}

			for (size_t i = 0; i < m_Pool.size(); ++i)
			{
				if (emptySet.find(&m_Pool[i]) == emptySet.end())
				{
					//m_Pool[i].Release();
				}
			}
			emptySet.clear();
			m_Pool.clear();
			m_EmptySpaces.clear();
			m_DirtyResources.clear();
		}

		template<typename...TArgs>
		castl::shared_ptr<T> AllocShared(TArgs&&...Args)
		{
			return castl::shared_ptr<T>(Alloc(castl::forward<TArgs>(Args)...), [this](T* releaseObj) { Release(releaseObj); });
		}

		bool IsEmpty()
		{
			castl::lock_guard<castl::recursive_mutex> lockGuard(m_Mutex);
			return m_EmptySpaces.size() == m_Pool.size();
		}

		void TickUpload(thread_management::CTaskGraph* pWorkingGraph)
		{
			castl::lock_guard<castl::recursive_mutex> lockGuard(m_Mutex);
			pWorkingGraph->SetupFunctor
			([this, dirtyResources = castl::move(m_DirtyResources)](thread_management::CTaskGraph* thisGraph)
			{
				thisGraph->NewTaskParallelFor()
				->JobCount(dirtyResources.size())
				->Functor
				([this, dirtyResources](size_t index)
				{
					if (dirtyResources[index]->IsValid())
					{
						auto pResource = dirtyResources[index];
						pResource->TickUpload();
						if (pResource->IsDirty())
						{
							EnqueueTickingResource(pResource);
						}
					}
				});
			});
			m_DirtyResources.clear();
		}

		virtual void EnqueueTickingResource(BaseTickingUpdateResource* resource) override
		{
			CA_ASSERT(resource != nullptr && resource->GetOwningUpdator() == this, "Invalid Ticking Resource Enqueue");
			T* pResource = dynamic_cast<T*>(resource);
			castl::lock_guard<castl::recursive_mutex> lockGuard(m_Mutex);
			m_DirtyResources.push_back(pResource);
		}

	private:
		CVulkanApplication& m_Owner;
		castl::recursive_mutex m_Mutex;
		castl::deque<T> m_Pool;
		castl::deque<T*> m_EmptySpaces;
		castl::deque<T*> m_DirtyResources;
	};
}
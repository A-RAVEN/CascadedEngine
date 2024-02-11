#pragma once
#include "RenderBackendSettings.h"
#include "VulkanApplicationSubobjectBase.h"
#include "TaskScheduling.h"
#include <ThreadManager.h>
#include <deque>

namespace graphics_backend
{
	using namespace thread_management;
	class BaseTickingUpdateResource;


	class BaseUploadingResource : public VKAppSubObjectBaseNoCopy
	{
	public:
		BaseUploadingResource(CVulkanApplication& owner);
		void Upload();
		void UploadAsync(UploadingResourceType resourceType);
		bool UploadingDone() const;
	protected:
		virtual void DoUpload() = 0;
		void MarkUploadingDoneThisFrame();
	private:
		FrameType m_SubmitFrame = INVALID_FRAMEID;
	};

	
}
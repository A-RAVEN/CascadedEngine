#pragma once
#include <Utils/VulkanIncludes.h>
#include <CACore/CASharedDic.h>

namespace graphics_backend
{
	template<typename T>
	class HashContainer
	{
	private:
		castl::shared_dic<VKHashVal, T>
	};
}
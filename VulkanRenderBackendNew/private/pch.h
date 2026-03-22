#pragma once

// Vulkan headers
#include <Utils/VulkanIncludes.h>

// Engine common headers
#include <CASTL/CAString.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAMutex.h>
#include <CASTL/CAAlgorithm.h>
#include <CACore/CALog.h>
#include <CACore/CAHash.h>

// Interface headers
#include <CRenderBackend.h>
#include <GPUBuffer.h>
#include <GPUTexture.h>
#include <WindowHandle.h>
#include <ShaderStruct.h>
#include <Common.h>

// VMA
#include <vk_mem_alloc.h>

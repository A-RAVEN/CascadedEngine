#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif
#define NOMINMAX
#include <windows.h>

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <directx/d3dx12.h>
#include <wrl.h>

#define D3D12MA_USING_DIRECTX_HEADERS 1
#include "D3D12MemAlloc.h"

using Microsoft::WRL::ComPtr;

#define D3D12_RENDER_BACKEND_DEBUG 1
#ifdef NDEBUG
#define D3D12_RENDER_BACKEND_DEBUG 1
#endif

#ifndef D3D12_RENDER_BACKEND_DEBUG
#define D3D12_RENDER_BACKEND_DEBUG 0
#endif
//#include <shellapi.h>
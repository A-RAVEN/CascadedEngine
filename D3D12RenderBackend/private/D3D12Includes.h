#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
//#include <directx/d3dx12.h>

//#include <string>
#include <wrl.h>
using Microsoft::WRL::ComPtr;

#define D3D12_RENDER_BACKEND_DEBUG 1
#ifdef NDEBUG
#define D3D12_RENDER_BACKEND_DEBUG 1
#endif

#ifndef D3D12_RENDER_BACKEND_DEBUG
#define D3D12_RENDER_BACKEND_DEBUG 0
#endif
//#include <shellapi.h>
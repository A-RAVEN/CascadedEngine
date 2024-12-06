#pragma once
#include "D3D12Includes.h"

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        //throw HrException(hr);
    }
}

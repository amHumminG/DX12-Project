#pragma once

#include "DX12.h"

inline void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) throw std::exception();
}
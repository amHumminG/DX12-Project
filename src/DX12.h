#pragma once

// --- WINDOWS ---
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

// --- DIRECTX12 ---
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include "d3dx12/d3dx12.h"

// --- WINDOWS RUNTIME LIBRARY ---
#include <wrl.h> // Com pointers
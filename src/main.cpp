#include "DX12.h"

// Agility SDK
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

#if defined(CreateWindow)
#undef CreateWindow
#endif

#include "Helpers.h"

#include <algorithm>
#include <cassert>
#include <chrono>

using namespace Microsoft::WRL;

const uint8_t g_NumFrames = 3; // The number of swap chain buffers
bool g_UseWarp = false; // WARP adapter

uint32_t g_ClientWidth = 1280;
uint32_t g_ClientHeight = 720;

bool g_IsInitialized = false; // True once all DX12 objects have been initialized

HWND g_hWnd; // Window handle
RECT g_WindowRect; // Window rectangle (Used to store window dimensions before going into fullscreen)

// DirectX12 objects
ComPtr<ID3D12Device2> g_Device;
ComPtr<ID3D12CommandQueue> g_CommandQueue;
ComPtr<IDXGISwapChain4> g_SwapChain;
ComPtr<ID3D12Resource> g_BackBuffers[g_NumFrames];
ComPtr<ID3D12GraphicsCommandList> g_CommandList;
ComPtr<ID3D12CommandAllocator> g_CommandAllocators[g_NumFrames];
ComPtr<ID3D12DescriptorHeap> g_RTVDescriptorHeap;
UINT g_RTVDescriptorSize;
UINT g_CurrentBackBufferIndex;

// Synchronization objects
ComPtr<ID3D12Fence> g_Fence;
uint64_t g_FenceValue = 0;
uint64_t g_FrameFenceValues[g_NumFrames] = {};
HANDLE g_FenceEvent;

// Present settings
bool g_Vsync = true;
bool g_TearingSupported = false;
bool g_Fullscreen = false;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM); // Window callback function





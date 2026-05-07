#pragma once

#include "DX12.h"

#include "Window.h"
#include "CommandQueue.h"

#include <memory>
#include <chrono>

class Renderer {
public:
	Renderer(std::unique_ptr<Window>* window);
	~Renderer() = default;

	bool Initialize();

	void Render();

private:
	bool m_isInitialized = false;

	std::unique_ptr<Window>* m_windowPtr;

	static const uint8_t	m_NumFrames = 3; // The number of swap chain buffers
	bool					m_UseWarp = false; // WARP adapter

	// DirectX12 objects
	Microsoft::WRL::ComPtr<ID3D12Device2>			m_Device;
	std::unique_ptr<CommandQueue>					m_CommandQueue;
	Microsoft::WRL::ComPtr<IDXGISwapChain4>			m_SwapChain;
	Microsoft::WRL::ComPtr<ID3D12Resource>			m_BackBuffers[m_NumFrames];
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>	m_RTVDescriptorHeap;
	UINT											m_RTVDescriptorSize;
	UINT											m_CurrentBackBufferIndex;

	// Synchronization objects
	Microsoft::WRL::ComPtr<ID3D12Fence>	m_Fence;
	uint64_t							m_FenceValue = 0;
	uint64_t							m_FrameFenceValues[m_NumFrames] = {};
	HANDLE								m_FenceEvent;

	// Present settings
	bool m_VSync = true;
	bool m_TearingSupported = false;
	bool m_Fullscreen = false;

	// Initialization helper functions
	bool CheckTearingSupport();
	void EnableDebugLayer();
	Microsoft::WRL::ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp);
	Microsoft::WRL::ComPtr<ID3D12Device2> CreateDevice(Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter);
	Microsoft::WRL::ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd, Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue,
		uint32_t width, uint32_t height, uint32_t bufferCount);
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(Microsoft::WRL::ComPtr<ID3D12Device2> device,
		D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);
	void UpdateRenderTargetViews(Microsoft::WRL::ComPtr<ID3D12Device2> device, Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain,
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap);
};
#pragma once

#include "DX12.h"

#include "Window.h"
#include "CommandQueue.h"
#include "Data.h"

#include <d3d12.h>
#include <memory>
#include <chrono>
#include <DirectXMath.h>

#include "ConstantBuffer.h"

// Vertex data for a colored cube.

class Renderer {
public:
	Renderer(std::unique_ptr<Window>* window);
	~Renderer() = default;

	bool Initialize();

	void Render();

	ConstantBuffer *GetConstantBuffer();

private:
	bool m_isInitialized = false;

	std::unique_ptr<Window>* m_windowPtr;

	static const uint8_t	m_numFrames = 3; // The number of swap chain buffers
	bool					m_useWarp = false; // WARP adapter

	// DirectX12 objects
	Microsoft::WRL::ComPtr<ID3D12Device2>			m_device;
	std::unique_ptr<CommandQueue>					m_commandQueue;
	Microsoft::WRL::ComPtr<IDXGISwapChain4>			m_swapChain;
	Microsoft::WRL::ComPtr<ID3D12Resource>			m_backBuffers[m_numFrames];
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>	m_RTVDescriptorHeap;
	UINT											m_RTVDescriptorSize;
	UINT											m_currentBackBufferIndex;

	// Synchronization objects
	Microsoft::WRL::ComPtr<ID3D12Fence>	m_fence;
	uint64_t							m_fenceValue = 0;
	uint64_t							m_frameFenceValues[m_numFrames] = {};
	HANDLE								m_fenceEvent;

	// Present settings
	bool m_vSync = true;
	bool m_tearingSupported = false;
	bool m_fullscreen = false;

	/// Content objects
	// Vertex buffer for the cube.
	Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	// Index buffer for the cube.
	Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView;

	// Depth buffer.
	Microsoft::WRL::ComPtr<ID3D12Resource> m_depthBuffer;
	// Descriptor heap for depth buffer.
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DSVHeap;

	// Root signature
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;

	// Pipeline state object.
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_volFogPSO; // Pipeline state object for pixel shader volumetric fog

	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;

	float m_fov;
	DirectX::XMMATRIX m_modelMatrix;

	bool m_contentLoaded;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CBVDescriptorHeap;

	std::unique_ptr<ConstantBuffer> m_constantBuffer;

	std::unique_ptr<ConstantBuffer> m_cameraPS;
	std::unique_ptr<ConstantBuffer> m_rayDataPS;

	/// Initialization helper functions
	// DirectX12 objects
	bool CheckTearingSupport();
	void EnableDebugLayer();
	Microsoft::WRL::ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp);
	Microsoft::WRL::ComPtr<ID3D12Device2> CreateDevice(Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter);
	Microsoft::WRL::ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd, Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue,
		uint32_t width, uint32_t height, uint32_t bufferCount);
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(Microsoft::WRL::ComPtr<ID3D12Device2> device, uint32_t numDescriptors,
		D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
	void UpdateRenderTargetViews(Microsoft::WRL::ComPtr<ID3D12Device2> device, Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain,
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap);

	bool LoadContent();
	void InitializeConstantBuffers();
	void UpdateBufferResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList, ID3D12Resource **pDestinationResource,
		ID3D12Resource **pIntermediateResource, size_t numElements, size_t elementSize, const void *bufferData, 
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);
	void ResizeDepthBuffer(int width, int height);

};
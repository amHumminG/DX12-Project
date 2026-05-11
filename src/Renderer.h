#pragma once

#include "DX12.h"

#include "Window.h"
#include "CommandQueue.h"

#include <d3d12.h>
#include <memory>
#include <chrono>
#include <DirectXMath.h>

// Vertex data for a colored cube.
struct VertexPosColor
{
	DirectX::XMFLOAT3 Position;
	DirectX::XMFLOAT3 Color;
};
static VertexPosColor g_Vertices[8] = {
	{ DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f), DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f) }, // 0
	{ DirectX::XMFLOAT3(-1.0f,  1.0f, -1.0f), DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f) }, // 1
	{ DirectX::XMFLOAT3(1.0f,  1.0f, -1.0f), DirectX::XMFLOAT3(1.0f, 1.0f, 0.0f) }, // 2
	{ DirectX::XMFLOAT3(1.0f, -1.0f, -1.0f), DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f) }, // 3
	{ DirectX::XMFLOAT3(-1.0f, -1.0f,  1.0f), DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f) }, // 4
	{ DirectX::XMFLOAT3(-1.0f,  1.0f,  1.0f), DirectX::XMFLOAT3(0.0f, 1.0f, 1.0f) }, // 5
	{ DirectX::XMFLOAT3(1.0f,  1.0f,  1.0f), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f) }, // 6
	{ DirectX::XMFLOAT3(1.0f, -1.0f,  1.0f), DirectX::XMFLOAT3(1.0f, 0.0f, 1.0f) }  // 7
};
static WORD g_Indicies[36] =
{
	0, 1, 2, 0, 2, 3,
	4, 6, 5, 4, 7, 6,
	4, 5, 1, 4, 1, 0,
	3, 2, 6, 3, 6, 7,
	1, 5, 6, 1, 6, 2,
	4, 0, 3, 4, 3, 7
};

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

	/// Content objects
	// Vertex buffer for the cube.
	Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
	// Index buffer for the cube.
	Microsoft::WRL::ComPtr<ID3D12Resource> m_IndexBuffer;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;

	// Depth buffer.
	Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthBuffer;
	// Descriptor heap for depth buffer.
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DSVHeap;

	// Root signature
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;

	// Pipeline state object.
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;

	D3D12_VIEWPORT m_Viewport;
	D3D12_RECT m_ScissorRect;

	float m_FoV;

	DirectX::XMMATRIX m_ModelMatrix;
	DirectX::XMMATRIX m_ViewMatrix;
	DirectX::XMMATRIX m_ProjectionMatrix;

	bool m_ContentLoaded;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CBVDescriptorHeap;

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
	void UpdateBufferResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList, ID3D12Resource **pDestinationResource,
		ID3D12Resource **pIntermediateResource, size_t numElements, size_t elementSize, const void *bufferData, 
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);
	void ResizeDepthBuffer(int width, int height);
};
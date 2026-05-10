#include "Renderer.h"

#include "Helpers.h"

#include <d3d12.h>

Renderer::Renderer(std::unique_ptr<Window>* window)
{
	m_windowPtr = window;
}

bool Renderer::Initialize()
{
	if (m_isInitialized) return true;

	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	EnableDebugLayer();

	Microsoft::WRL::ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(m_UseWarp);
	m_Device = CreateDevice(dxgiAdapter4);
	m_CommandQueue = std::make_unique<CommandQueue>(m_Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_SwapChain = CreateSwapChain(m_windowPtr->get()->GetHWND(), m_CommandQueue->GetD3D12CommandQueue(),
		m_windowPtr->get()->GetWidth(), m_windowPtr->get()->GetHeight(), m_NumFrames);
	m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
	m_RTVDescriptorHeap = CreateDescriptorHeap(m_Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, m_NumFrames);
	m_RTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	UpdateRenderTargetViews(m_Device, m_SwapChain, m_RTVDescriptorHeap);

	m_isInitialized = true;

	return true;
}

void Renderer::Render()
{
	if (!m_isInitialized) return;

	Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer = m_BackBuffers[m_CurrentBackBufferIndex];

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList = m_CommandQueue->GetCommandList();

	// Clear the render target
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			backBuffer.Get(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
		);

		commandList->ResourceBarrier(1, &barrier);

		FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			m_CurrentBackBufferIndex, m_RTVDescriptorSize);

		commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	}

	// Present
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			backBuffer.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		);

		commandList->ResourceBarrier(1, &barrier);

		ID3D12CommandList *const commandLists[] = { commandList.Get() };
		m_CommandQueue->ExecuteCommandList(commandList);

		UINT syncInterval = m_VSync ? 1 : 0;
		UINT presentFlags = m_TearingSupported && !m_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
		ThrowIfFailed(m_SwapChain->Present(syncInterval, presentFlags));

		uint64_t frameFenceValue = m_CommandQueue->Signal();

		m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
		m_CommandQueue->WaitForFenceValue(frameFenceValue);
	}
}

bool Renderer::CheckTearingSupport() {
	BOOL allowTearing = FALSE;

	// Rather than create the DXGI 1.5 factory interface directly, we create the
	// DXGI 1.4 interface and query for the 1.5 interface. This is to enable the
	// graphics debugging tools which will not support the 1.5 factory interface
	// until a future update.
	Microsoft::WRL::ComPtr<IDXGIFactory4> factory4;
	if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4)))) {
		Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
		if (SUCCEEDED(factory4.As(&factory5))) {
			if (FAILED(factory5->CheckFeatureSupport(
				DXGI_FEATURE_PRESENT_ALLOW_TEARING,
				&allowTearing, sizeof(allowTearing)))) {
				allowTearing = FALSE;
			}
		}
	}

	return allowTearing == TRUE;
}

void Renderer::EnableDebugLayer() {
#if defined (_DEBUG)
	// All errors generated when creating DX12 objects are caught by the
	// debug layer
	Microsoft::WRL::ComPtr<ID3D12Debug> debugInterface;
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
	debugInterface->EnableDebugLayer();
#endif
}

Microsoft::WRL::ComPtr<IDXGIAdapter4> Renderer::GetAdapter(bool useWarp) {
	Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;
	UINT createFactoryFlags = 0;
#if defined (_DEBUG)
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

	Microsoft::WRL::ComPtr<IDXGIAdapter1> dxgiAdapter1;
	Microsoft::WRL::ComPtr<IDXGIAdapter4> dxgiAdapter4;

	if (useWarp) {
		ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
		ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
	}
	else {
		SIZE_T maxDedicatedVideoMemory = 0;
		for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i) {
			DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
			dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

			// Check to see if the adapter can create a device (without actually crating it)
			// We then return the GPU adapter with the most video memory (usually the main graphics card)
			if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
				SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(),
					D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
				dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory) {
				maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
				ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
			}
		}
	}

	return dxgiAdapter4;
}

Microsoft::WRL::ComPtr<ID3D12Device2> Renderer::CreateDevice(Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter) {
	Microsoft::WRL::ComPtr<ID3D12Device2> d3d12Device2;
	ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));

	// Enable debug messages
#if defined(_DEBUG)
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> pInfoQueue;
	if (SUCCEEDED(d3d12Device2.As(&pInfoQueue))) {
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

		// Suppress messages based on severity level
		D3D12_MESSAGE_SEVERITY Severities[] = { D3D12_MESSAGE_SEVERITY_INFO };

		// Suppress messages based on ID
		D3D12_MESSAGE_ID DenyIds[] = {
		  D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
		  D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
		  D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE
		};

		D3D12_INFO_QUEUE_FILTER NewFilter = {};
		NewFilter.DenyList.NumSeverities = _countof(Severities);
		NewFilter.DenyList.pSeverityList = Severities;
		NewFilter.DenyList.NumIDs = _countof(DenyIds);
		NewFilter.DenyList.pIDList = DenyIds;

		ThrowIfFailed(pInfoQueue->PushStorageFilter(&NewFilter));
	}
#endif

	return d3d12Device2;
}

Microsoft::WRL::ComPtr<IDXGISwapChain4> Renderer::CreateSwapChain(HWND hWnd, Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue,
	uint32_t width, uint32_t height, uint32_t bufferCount) {
	Microsoft::WRL::ComPtr<IDXGISwapChain4> dxgiSwapChain4;
	Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory4;
	UINT createFactoryFlags = 0;

#if defined(_DEBUG)
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = FALSE;
	swapChainDesc.SampleDesc = { 1, 0 };
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = bufferCount;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapChainDesc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
		commandQueue.Get(),
		hWnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain1
	));

	// Disable Alt + Enter fullscreen toggle feature (we will handle this manually)
	ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain1.As(&dxgiSwapChain4));

	return dxgiSwapChain4;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> Renderer::CreateDescriptorHeap(Microsoft::WRL::ComPtr<ID3D12Device2> device, 
	D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors) {
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = numDescriptors;
	desc.Type = type;

	ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

	return descriptorHeap;
}

void Renderer::UpdateRenderTargetViews(Microsoft::WRL::ComPtr<ID3D12Device2> device, Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain,
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap) {
	auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

	for (int i = 0; i < m_NumFrames; i++) {
		Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;
		ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
		m_BackBuffers[i] = backBuffer;
		rtvHandle.Offset(rtvDescriptorSize);
	}
}

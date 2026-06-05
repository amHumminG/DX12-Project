#include "Renderer.h"

#include "Helpers.h"
#include "Data.h"

#include <d3d12.h>
#include <DirectXMath.h>

Renderer::Renderer(std::unique_ptr<Window>* window)
{
	m_windowPtr = window;
}

bool Renderer::Initialize(const Scene &scene)
{
	if (m_isInitialized) return true;

	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	EnableDebugLayer();

	Microsoft::WRL::ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(m_useWarp);
	m_device = CreateDevice(dxgiAdapter4);
	m_commandQueue = std::make_unique<CommandQueue>(m_device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	m_swapChain = CreateSwapChain(m_windowPtr->get()->GetHWND(), m_commandQueue->GetD3D12CommandQueue(),
		m_windowPtr->get()->GetWidth(), m_windowPtr->get()->GetHeight(), m_numFrames);
	m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps
	m_rtvDescriptorHeap		= CreateDescriptorHeap(m_device, m_numFrames + 1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_rtvDescriptorSize		= m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_dsvDescriptorHeap		= CreateDescriptorHeap(m_device, m_numFrames, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_dsvDescriptorSize		= m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_resourceDescriptorHeap = CreateDescriptorHeap(m_device, 20, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
								D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
	m_resourceDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	UpdateRenderTargetViews();

	if (!LoadContent(scene)) return false;

	m_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(m_windowPtr->get()->GetWidth()), static_cast<float>(m_windowPtr->get()->GetHeight()));
	m_scissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

	m_isInitialized = true;

	return true;
}

void Renderer::Render(const Scene &scene)
{
	if (!m_isInitialized) return;

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList = m_commandQueue->GetCommandList();

	RenderShadowMaps(scene, commandList);

	Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer = m_backBuffers[m_currentBackBufferIndex];

	// --- Setup and Clear RTV's and DSV ---
	CD3DX12_CPU_DESCRIPTOR_HANDLE backBufferRTV(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		m_currentBackBufferIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE sceneColorRTV(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		3, m_rtvDescriptorSize);
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	CD3DX12_RESOURCE_BARRIER barrier;
	if (m_sceneColorState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
		barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_sceneColor.Get(),
			m_sceneColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

		commandList->ResourceBarrier(1, &barrier);
	}

	barrier = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer.Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	commandList->ResourceBarrier(1, &barrier);

	// Transition depth buffer to RTV
	if (m_depthBuffer.state != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
		barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_depthBuffer.depthBuffer.Get(),
			m_depthBuffer.state,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		);
		m_depthBuffer.state = D3D12_RESOURCE_STATE_DEPTH_WRITE;

		commandList->ResourceBarrier(1, &barrier);
	}

	FLOAT clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	commandList->ClearRenderTargetView(sceneColorRTV, clearColor, 0, nullptr);
	commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	commandList->OMSetRenderTargets(1, &sceneColorRTV, FALSE, &dsv);

	// --- Draw scene to scene color texture ---
	commandList->SetPipelineState(m_pipelineState.Get());
	commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->RSSetViewports(1, &m_viewport);
	commandList->RSSetScissorRects(1, &m_scissorRect);

	// Bind the resource heap
	ID3D12DescriptorHeap *heaps[] = { m_resourceDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(1, heaps);

	commandList->SetGraphicsRootDescriptorTable(0, m_perObject->GetGPUDescriptorHandle());

	// --- Draw Scene ---
	PerFrame perFrame = scene.GetCamera();
	m_perFrame->Update(&perFrame, sizeof(PerFrame));
	commandList->SetGraphicsRootDescriptorTable(1, m_perFrame->GetGPUDescriptorHandle());

	for (const PerObject &instance : scene.GetCubeInstances()) {
		m_perObject->Update(&instance, sizeof(PerObject));
		commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
		commandList->IASetIndexBuffer(&m_indexBufferView);
		commandList->DrawIndexedInstanced(_countof(Cube::indices), 1, 0, 0, 0);
	}

	Camera camera = scene.GetCameraConstBuff();
	m_cameraPS->Update(&camera, sizeof(Camera));

	if (m_useFog) {
		if (!m_useComputeshaderFog) {
			RenderPSFog(commandList, backBuffer);
		}
		else {
			RenderCSFog(commandList, backBuffer);
		}
	}
	else {
		if (m_sceneColorState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			   m_sceneColor.Get(),
			   m_sceneColorState,
			   D3D12_RESOURCE_STATE_COPY_SOURCE
			);
			m_sceneColorState = D3D12_RESOURCE_STATE_COPY_SOURCE;
			commandList->ResourceBarrier(1, &barrier);
		}

		CD3DX12_RESOURCE_BARRIER copyDestBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		   backBuffer.Get(),
		   D3D12_RESOURCE_STATE_RENDER_TARGET,
		   D3D12_RESOURCE_STATE_COPY_DEST
		);
		commandList->ResourceBarrier(1, &copyDestBarrier);

		// Copy texture to back buffer
		commandList->CopyResource(backBuffer.Get(), m_sceneColor.Get());

		CD3DX12_RESOURCE_BARRIER presentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		   backBuffer.Get(),
		   D3D12_RESOURCE_STATE_COPY_DEST,
		   D3D12_RESOURCE_STATE_PRESENT
		);
		commandList->ResourceBarrier(1, &presentBarrier);
	}

	m_commandQueue->ExecuteCommandList(commandList);
	UINT syncInterval = m_vSync ? 1 : 0;
	UINT presentFlags = m_tearingSupported && !m_vSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
	ThrowIfFailed(m_swapChain->Present(syncInterval, presentFlags));

	uint64_t frameFenceValue = m_commandQueue->Signal();
	m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
	m_commandQueue->WaitForFenceValue(frameFenceValue);
}

void Renderer::RenderPSFog(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
	Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer)
{
	// Transition the scene color SRV for pixel shader
	if (m_sceneColorState != D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) {
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_sceneColor.Get(),
			m_sceneColorState,
			D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE
		);
		m_sceneColorState = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &barrier);
	}

	// Transition the depth buffer to an SRV for pixel shader
	if (m_depthBuffer.state != D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) {
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_depthBuffer.depthBuffer.Get(),
			m_depthBuffer.state,
			D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE
		);
		m_depthBuffer.state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &barrier);
	}

	// Bind the back buffer as the render target
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
		m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		m_currentBackBufferIndex,
		m_rtvDescriptorSize
	);
	commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	// Draw the full screen quad
	commandList->SetPipelineState(m_volFogPSO.Get());
	commandList->SetGraphicsRootDescriptorTable(2, m_cameraPS->GetGPUDescriptorHandle()); // Bind camera data
	commandList->SetGraphicsRootDescriptorTable(3, m_rayDataPS->GetGPUDescriptorHandle()); // Bind ray data

	// Bind SRV table
	CD3DX12_GPU_DESCRIPTOR_HANDLE srvTableHandle(m_resourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		5, m_resourceDescriptorSize);
	commandList->SetGraphicsRootDescriptorTable(4, srvTableHandle);

	commandList->DrawInstanced(3, 1, 0, 0);

	// Transition the back buffer to present
	CD3DX12_RESOURCE_BARRIER presentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		backBuffer.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);
	commandList->ResourceBarrier(1, &presentBarrier);
}

void Renderer::RenderCSFog(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
	Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer)
{
	// Transition the scene color SRV for compute
	if (m_sceneColorState != D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) {
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_sceneColor.Get(),
			m_sceneColorState,
			D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE
		);
		m_sceneColorState = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &barrier);
	}

	// Transition the depth buffer to an SRV for compute
	if (m_depthBuffer.state != D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) {
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_depthBuffer.depthBuffer.Get(),
			m_depthBuffer.state,
			D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE
		);
		m_depthBuffer.state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &barrier);
	}

	auto computeTexture = m_computeOutputTextures[m_currentBackBufferIndex];
	commandList->SetPipelineState(m_computePipelineState.Get());
	commandList->SetComputeRootSignature(m_computeRootSignature.Get());

	// Bind the UAV output texture
	CD3DX12_GPU_DESCRIPTOR_HANDLE uavGpuHandle(
		m_resourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		13 + m_currentBackBufferIndex,
		m_resourceDescriptorSize
	);
	commandList->SetComputeRootDescriptorTable(0, uavGpuHandle);

	// Bind the camera data CBV
	CD3DX12_GPU_DESCRIPTOR_HANDLE cameraCbvHandle(
		m_resourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		3,
		m_resourceDescriptorSize
	);
	commandList->SetComputeRootDescriptorTable(1, cameraCbvHandle);

	// Bind rayData
	CD3DX12_GPU_DESCRIPTOR_HANDLE rayDataHandle(
		m_resourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		4,
		m_resourceDescriptorSize
	);
	commandList->SetComputeRootDescriptorTable(2, rayDataHandle);

	// Bind the SRV table (sceneColor and depth buffer)
	CD3DX12_GPU_DESCRIPTOR_HANDLE srvTableHandle(
		m_resourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		5,
		m_resourceDescriptorSize
	);
	commandList->SetComputeRootDescriptorTable(3, srvTableHandle);

	// Dispatch compute shader
	commandList->Dispatch(
		(m_windowPtr->get()->GetWidth() + 7) / 8,
		(m_windowPtr->get()->GetHeight() + 7) / 8,
		1
	);

	// Transition textures so we can copy the compute texture to the back buffer
	CD3DX12_RESOURCE_BARRIER copyBarriers[2];
	copyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
	computeTexture.Get(),
	D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
	D3D12_RESOURCE_STATE_COPY_SOURCE
	);
	copyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
		backBuffer.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COPY_DEST
	);
	commandList->ResourceBarrier(2, copyBarriers);

	// Copy the compute output texture to the backbuffer
	commandList->CopyResource(backBuffer.Get(), computeTexture.Get());

	// Transition the compute output texture back to UAV and the backbuffer to PRESENT
	CD3DX12_RESOURCE_BARRIER restoreBarriers[2];
	restoreBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
	computeTexture.Get(),
	D3D12_RESOURCE_STATE_COPY_SOURCE,
	D3D12_RESOURCE_STATE_UNORDERED_ACCESS
	);
	restoreBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
		backBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PRESENT
	);
	commandList->ResourceBarrier(2, restoreBarriers);
}

void Renderer::ToggleFog() {
	m_useFog = !m_useFog;
}

void Renderer::ToggleComputeShaderFog() {
	m_useComputeshaderFog = !m_useComputeshaderFog;
}

ConstantBuffer *Renderer::GetConstantBuffer()
{
	return m_perObject.get();
}

ConstantBuffer *Renderer::GetRayDataConstantBuffer()
{
	return m_rayDataPS.get();
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

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> Renderer::CreateDescriptorHeap(Microsoft::WRL::ComPtr<ID3D12Device2> device, uint32_t numDescriptors,
	D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags) {
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = numDescriptors;
	desc.Type = type;
	desc.Flags = flags;

	ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

	return descriptorHeap;
}

void Renderer::UpdateRenderTargetViews() {
	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_resourceDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_resourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	uavHandle.Offset(13, m_resourceDescriptorSize);

	for (int i = 0; i < m_numFrames; i++) {
		Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;
		ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
		m_backBuffers[i] = backBuffer;

		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
		CD3DX12_RESOURCE_DESC computeTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R8G8B8A8_UNORM,
			m_windowPtr->get()->GetWidth(),
			m_windowPtr->get()->GetHeight(),
			1,
			1,
			1,
			0,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		ThrowIfFailed(m_device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&computeTextureDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&m_computeOutputTextures[i])
		));

		m_device->CreateUnorderedAccessView(m_computeOutputTextures[i].Get(), nullptr, nullptr, uavHandle);

		rtvHandle.Offset(m_rtvDescriptorSize);
		uavHandle.Offset(m_resourceDescriptorSize);
	}
}

bool Renderer::InitializeComputeRootSignature() {
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	// Output texture (u0)
	CD3DX12_DESCRIPTOR_RANGE uavRange;
	uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	// Camera/light data cbv (b0)
	CD3DX12_DESCRIPTOR_RANGE camRange;
	camRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	// Ray data (b1)
	CD3DX12_DESCRIPTOR_RANGE rayDataRange;
	rayDataRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	// Texture data srv (t0 -> t7)
	CD3DX12_DESCRIPTOR_RANGE srvRange;
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0);

	// Create root parameters
	CD3DX12_ROOT_PARAMETER computeRootParameters[4];
	computeRootParameters[0].InitAsDescriptorTable(1, &uavRange);
	computeRootParameters[1].InitAsDescriptorTable(1, &camRange);
	computeRootParameters[2].InitAsDescriptorTable(1, &rayDataRange);
	computeRootParameters[3].InitAsDescriptorTable(1, &srvRange);

	CD3DX12_STATIC_SAMPLER_DESC staticSampler = {};
	staticSampler.ShaderRegister = 0; // (s0)
	staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// Root signature description
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;
	computeRootSignatureDesc.Init_1_0(
		_countof(computeRootParameters),
		computeRootParameters,
		1,
		&staticSampler,
		D3D12_ROOT_SIGNATURE_FLAG_NONE
	);

	// Serialize and create root signature
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(
		&computeRootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1_1,
		&signatureBlob,
		&errorBlob
	);

	if (FAILED(hr)) {
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		return false;
	}

	ThrowIfFailed(m_device->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&m_computeRootSignature)
	));

	return true;
}

bool Renderer::LoadContent(const Scene &scene)
{
	// Resize/Create the depth buffer.
	ResizeDepthBuffer(m_windowPtr->get()->GetWidth(), m_windowPtr->get()->GetHeight());

	CreateLights(scene);

	CreateRootSignature();
	InitializeComputeRootSignature();

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList = m_commandQueue->GetCommandList();

	Cube cube;

	// Upload vertex buffer data.
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateVertexBuffer;
	UpdateBufferResource(commandList,
		&m_vertexBuffer, &intermediateVertexBuffer,
		_countof(cube.vertices), sizeof(VertexPosColor), cube.vertices);

	// Create the vertex buffer view.
	m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
	m_vertexBufferView.SizeInBytes = sizeof(cube.vertices);
	m_vertexBufferView.StrideInBytes = sizeof(VertexPosColor);

	// Upload index buffer data.
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateIndexBuffer;
	UpdateBufferResource(commandList,
		&m_indexBuffer, &intermediateIndexBuffer,
		_countof(cube.indices), sizeof(WORD), cube.indices);

	// Create index buffer view.
	m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
	m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
	m_indexBufferView.SizeInBytes = sizeof(cube.indices);

	// Load the vertex shader.
	Microsoft::WRL::ComPtr<ID3DBlob> vertexShaderBlob;
	ThrowIfFailed(D3DReadFileToBlob(L"VertexShader.cso", &vertexShaderBlob));

	// Load the pixel shader.
	Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderBlob;
	ThrowIfFailed(D3DReadFileToBlob(L"PixelShader.cso", &pixelShaderBlob));

	// Create the vertex input layout
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	struct PipelineStateStream
	{
		CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
		CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
		CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
		CD3DX12_PIPELINE_STATE_STREAM_VS VS;
		CD3DX12_PIPELINE_STATE_STREAM_PS PS;
		CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
		CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
	}pipelineStateStream;

	D3D12_RT_FORMAT_ARRAY rtvFormats = {};
	rtvFormats.NumRenderTargets = 1;
	rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	pipelineStateStream.pRootSignature = m_rootSignature.Get();
	pipelineStateStream.InputLayout = { inputLayout, _countof(inputLayout) };
	pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
	pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
	pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pipelineStateStream.RTVFormats = rtvFormats;

	D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
		sizeof(PipelineStateStream), &pipelineStateStream
	};
	ThrowIfFailed(m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState)));

	// --- Compute Shader Setup ---
	Microsoft::WRL::ComPtr<ID3DBlob> computeShaderBlob;
	ThrowIfFailed(D3DReadFileToBlob(L"VolumetricFogCS.cso", &computeShaderBlob));

	D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc = {};
	computePipelineStateDesc.pRootSignature = m_computeRootSignature.Get();
	computePipelineStateDesc.CS = CD3DX12_SHADER_BYTECODE(computeShaderBlob.Get());
	ThrowIfFailed(m_device->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&m_computePipelineState)));

	// Pipeline state for pixel shader volumetric fog
	{
		// Load the vertex shader.
		Microsoft::WRL::ComPtr<ID3DBlob> fullscreenVSBlob;
		ThrowIfFailed(D3DReadFileToBlob(L"FullScreen.cso", &fullscreenVSBlob));

		// Load the pixel shader.
		Microsoft::WRL::ComPtr<ID3DBlob> volFogPSBlob;
		ThrowIfFailed(D3DReadFileToBlob(L"VolumetricFogPS.cso", &volFogPSBlob));

		struct VolumetricFogPSO {
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
			CD3DX12_PIPELINE_STATE_STREAM_VS VS;
			CD3DX12_PIPELINE_STATE_STREAM_PS PS;
			CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		} volFogPipelineStateStream;
		volFogPipelineStateStream.pRootSignature = m_rootSignature.Get();
		volFogPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		volFogPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(fullscreenVSBlob.Get());
		volFogPipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(volFogPSBlob.Get());
		volFogPipelineStateStream.RTVFormats = rtvFormats;

		D3D12_PIPELINE_STATE_STREAM_DESC volFogPipelineStateStreamDesc = {
			sizeof(VolumetricFogPSO), &volFogPipelineStateStream
		};
		ThrowIfFailed(m_device->CreatePipelineState(&volFogPipelineStateStreamDesc, IID_PPV_ARGS(&m_volFogPSO)));
	}

	// Pipeline state for shadow mapping
	{
		// Load the vertex shader.
		ThrowIfFailed(D3DReadFileToBlob(L"VertexShader.cso", &vertexShaderBlob));

		struct ShadowMapPSO {
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
			CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
			CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
			CD3DX12_PIPELINE_STATE_STREAM_VS VS;
			CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
			CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
		} shadowPipelineStateStream;
		shadowPipelineStateStream.pRootSignature = m_rootSignature.Get();
		shadowPipelineStateStream.InputLayout = { inputLayout, _countof(inputLayout) };
		shadowPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		shadowPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
		shadowPipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		shadowPipelineStateStream.RTVFormats = rtvFormats;

		D3D12_PIPELINE_STATE_STREAM_DESC shadowPipelineStateStreamDesc = {
			sizeof(ShadowMapPSO), &shadowPipelineStateStream
		};
		ThrowIfFailed(m_device->CreatePipelineState(&shadowPipelineStateStreamDesc, IID_PPV_ARGS(&m_shadowMapPSO)));
	}

	auto fenceValue = m_commandQueue->ExecuteCommandList(commandList);
	m_commandQueue->WaitForFenceValue(fenceValue);

	m_contentLoaded = true;

	return true;
}

void Renderer::CreateRootSignature()
{
	const unsigned int nParameters = 5;
	CD3DX12_ROOT_PARAMETER1 rootParameters[nParameters];
	CD3DX12_DESCRIPTOR_RANGE1 ranges[nParameters];
	CD3DX12_STATIC_SAMPLER_DESC staticSampler = {};

	// Vertex shader
	{
		// b0
		PerObject perObject = {};
		DirectX::XMStoreFloat4x4(&perObject.model, DirectX::XMMatrixIdentity());
		m_perObject = std::make_unique<ConstantBuffer>(m_device, m_resourceDescriptorHeap, 0, &perObject, sizeof(PerObject));

		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);

		PerFrame perFrame = {};
		DirectX::XMStoreFloat4x4(&perFrame.view, DirectX::XMMatrixIdentity());
		DirectX::XMStoreFloat4x4(&perFrame.proj, DirectX::XMMatrixIdentity());
		m_perFrame = std::make_unique<ConstantBuffer>(m_device, m_resourceDescriptorHeap, 1, &perFrame, sizeof(PerFrame));
		m_shadowPerFrame = std::make_unique<ConstantBuffer>(m_device, m_resourceDescriptorHeap, 2, &perFrame, sizeof(PerFrame));

		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_VERTEX);
	}

	// Pixel shader
	{
		// b0
		Camera camera;
		camera.position = { 0.0f, 0.0f, -10.0f };

		// Create the view matrix.
		const DirectX::XMVECTOR eyePosition = DirectX::XMVectorSet(0, 0, -10, 1);
		const DirectX::XMVECTOR focusPoint = DirectX::XMVectorSet(0, 0, 0, 1);
		const DirectX::XMVECTOR upDirection = DirectX::XMVectorSet(0, 1, 0, 0);
		DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);

		// Create the projection matrix.
		float fov = 45.0f;
		float aspectRatio = 1280 / static_cast<float>(720);
		DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(fov), aspectRatio, 0.1f, 100.0f);
		DirectX::XMStoreFloat4x4(&camera.inverseViewProj, DirectX::XMMatrixInverse(nullptr, DirectX::XMMatrixMultiply(view, projection)));
		m_cameraPS = std::make_unique<ConstantBuffer>(m_device, m_resourceDescriptorHeap, 3, &camera, sizeof(Camera));

		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
		rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);

		// b1
		RayData rayData;
		rayData.raySteps = 32;
		rayData.frameCount = 1;
		m_rayDataPS = std::make_unique<ConstantBuffer>(m_device, m_resourceDescriptorHeap, 4, &rayData, sizeof(RayData));

		ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
		rootParameters[3].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_PIXEL);

		// t0 - t7
		{
			ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0);
			rootParameters[4].InitAsDescriptorTable(1, &ranges[4], D3D12_SHADER_VISIBILITY_PIXEL);

			// Texture2D
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

			// StructuredBuffer
			D3D12_SHADER_RESOURCE_VIEW_DESC sbViewDesc = {};
			sbViewDesc.Format = DXGI_FORMAT_UNKNOWN; // Must be UNKNOWN for structured buffers
			sbViewDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			sbViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			sbViewDesc.Buffer.FirstElement = 0;
			sbViewDesc.Buffer.NumElements = 1; // Number of structs inside the buffer
			sbViewDesc.Buffer.StructureByteStride = sizeof(int);
			sbViewDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			// Texture2DArray
			D3D12_SHADER_RESOURCE_VIEW_DESC arrayViewDesc = {};
			arrayViewDesc.Format = DXGI_FORMAT_R32_FLOAT;
			arrayViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			arrayViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			arrayViewDesc.Texture2DArray.MostDetailedMip = 0;
			arrayViewDesc.Texture2DArray.MipLevels = 1;
			arrayViewDesc.Texture2DArray.FirstArraySlice = 0;
			arrayViewDesc.Texture2DArray.ArraySize = 1; // Total textures in array

			// t0 Scene color
			CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_resourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 5, m_resourceDescriptorSize);
			SetupVolumetricFogPS();
			m_device->CreateShaderResourceView(m_sceneColor.Get(), &srvDesc, hDescriptor);

			// t1 Depth buffer
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
			m_device->CreateShaderResourceView(m_depthBuffer.depthBuffer.Get(), &srvDesc, hDescriptor);

			// t2 Spotlights
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			m_device->CreateShaderResourceView(nullptr, &sbViewDesc, hDescriptor);

			// t3 Spotlights shadow maps
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			m_device->CreateShaderResourceView(nullptr, &arrayViewDesc, hDescriptor);

			// t4 Directional light
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			m_device->CreateShaderResourceView(m_directionalLight.Get(), &sbViewDesc, hDescriptor);

			// t5 Directional shadow map
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			m_device->CreateShaderResourceView(m_directionalShadows.depthBuffer.Get(), &arrayViewDesc, hDescriptor);

			// t6 Point lights
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			m_device->CreateShaderResourceView(nullptr, &sbViewDesc, hDescriptor);

			// t7 Point lights shadow maps
			arrayViewDesc = {};
			arrayViewDesc.Format = DXGI_FORMAT_R32_FLOAT;
			arrayViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
			arrayViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			arrayViewDesc.TextureCubeArray.MostDetailedMip = 0;
			arrayViewDesc.TextureCubeArray.MipLevels = 1;
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			m_device->CreateShaderResourceView(nullptr, &arrayViewDesc, hDescriptor);
		}

		// s0
		staticSampler.ShaderRegister = 0;
		staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}

	// Allow input layout and deny unnecessary access to certain pipeline stages.
	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
	rootSignatureDescription.Init_1_1(_countof(rootParameters), rootParameters, 1, &staticSampler, rootSignatureFlags);

	// Create a root signature.
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	// Serialize the root signature.
	Microsoft::WRL::ComPtr<ID3DBlob> rootSignatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDescription,
		featureData.HighestVersion, &rootSignatureBlob, &errorBlob));

	// Create the root signature.
	ThrowIfFailed(m_device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
		rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
}

void Renderer::SetupVolumetricFogPS()
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = m_windowPtr->get()->GetWidth();
	desc.Height = m_windowPtr->get()->GetHeight();
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	clearValue.Color[0] = 0.0f;
	clearValue.Color[1] = 0.0f;
	clearValue.Color[2] = 0.0f;
	clearValue.Color[3] = 1.0f;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&m_sceneColor)));
	m_sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), m_numFrames, m_rtvDescriptorSize);
	m_device->CreateRenderTargetView(m_sceneColor.Get(), &rtvDesc, hDescriptor);
}

void Renderer::UpdateBufferResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList, ID3D12Resource **pDestinationResource,
	ID3D12Resource **pIntermediateResource, size_t numElements, size_t elementSize, const void *bufferData, D3D12_RESOURCE_FLAGS flags)
{
	size_t bufferSize = numElements * elementSize;

	// Create a committed resource for the GPU resource in a default heap.
	CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags);
	ThrowIfFailed(m_device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(pDestinationResource)));

	// Create an committed resource for the upload.
	if (bufferData)
	{
		heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
		ThrowIfFailed(m_device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(pIntermediateResource)));

		D3D12_SUBRESOURCE_DATA subresourceData = {};
		subresourceData.pData = bufferData;
		subresourceData.RowPitch = bufferSize;
		subresourceData.SlicePitch = subresourceData.RowPitch;

		UpdateSubresources(commandList.Get(),
			*pDestinationResource, *pIntermediateResource,
			0, 0, 1, &subresourceData);
	}
}

void Renderer::ResizeDepthBuffer(int width, int height)
{
	if (m_contentLoaded)
	{
		// Flush any GPU commands that might be referencing the depth buffer.
		m_commandQueue->Flush();

		width = std::max(1, width);
		height = std::max(1, height);

		// Resize screen dependent resources.
		// Create a depth buffer.
		D3D12_CLEAR_VALUE optimizedClearValue = {};
		optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
		optimizedClearValue.DepthStencil = { 1.0f, 0 };

		CD3DX12_HEAP_PROPERTIES heapProperty = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height,
			1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

		ThrowIfFailed(m_device->CreateCommittedResource(
			&heapProperty,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&optimizedClearValue,
			IID_PPV_ARGS(&m_depthBuffer.depthBuffer)
		));

		m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		// Update the depth-stencil view.
		D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
		dsv.Format = DXGI_FORMAT_D32_FLOAT;
		dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsv.Texture2D.MipSlice = 0;
		dsv.Flags = D3D12_DSV_FLAG_NONE;

		m_device->CreateDepthStencilView(m_depthBuffer.depthBuffer.Get(), &dsv,
			m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
			
		CreateDepthBuffer(width, height, 1, m_depthBuffer, 0);
	}
}

void Renderer::CreateDepthBuffer(int width, int height, unsigned int nBuffers, DepthBuffer &depthBuffer, uint32_t descriptorIndex)
{
	if (m_dsvDescriptorHeap == nullptr) {
		// Create the descriptor heap for the depth-stencil view.
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 4;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvDescriptorHeap)));
	}

	// Create a depth buffer.
	D3D12_CLEAR_VALUE optimizedClearValue = {};
	optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	optimizedClearValue.DepthStencil = { 1.0f, 0 };

	CD3DX12_HEAP_PROPERTIES heapProperty = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height,
		nBuffers, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

	ThrowIfFailed(m_device->CreateCommittedResource(
		&heapProperty,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&optimizedClearValue,
		IID_PPV_ARGS(&depthBuffer.depthBuffer)
	));
	depthBuffer.state = D3D12_RESOURCE_STATE_DEPTH_WRITE;

	// Update the depth-stencil view.
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
	dsv.Format = DXGI_FORMAT_D32_FLOAT;
	dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsv.Texture2D.MipSlice = 0;
	dsv.Flags = D3D12_DSV_FLAG_NONE;

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex,
		m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));
	m_device->CreateDepthStencilView(depthBuffer.depthBuffer.Get(), &dsv, hDescriptor);
}

void Renderer::CreateLights(const Scene &scene)
{
	// Directional light
	{
		DirectionalLight dirLight = scene.GetDirectionlLight();
		CreateStructuredBuffer(&dirLight, sizeof(DirectionalLight), m_directionalLight);
		CreateDepthBuffer(m_shadowMapSize, m_shadowMapSize, 1, m_directionalShadows, 1);
	}
}

void Renderer::CreateStructuredBuffer(void *data, UINT64 bufferSize, Microsoft::WRL::ComPtr<ID3D12Resource> &buffer)
{
	CD3DX12_HEAP_PROPERTIES gpuHeapProps(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

	ThrowIfFailed(m_device->CreateCommittedResource(
		&gpuHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&buffer)
	));

	Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
	CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);

	ThrowIfFailed(m_device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, // Required state for Upload heaps
		nullptr,
		IID_PPV_ARGS(&uploadBuffer)
	));

	void *pData = nullptr;
	CD3DX12_RANGE readRange(0, 0); // We do not intend to read this data on the CPU
	ThrowIfFailed(uploadBuffer->Map(0, &readRange, &pData));
	memcpy(pData, data, bufferSize);
	uploadBuffer->Unmap(0, nullptr);

	auto commandList = m_commandQueue->GetCommandList();

	CD3DX12_RESOURCE_BARRIER barrierCopyStart = CD3DX12_RESOURCE_BARRIER::Transition(
		buffer.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COPY_DEST
	);
	commandList->ResourceBarrier(1, &barrierCopyStart);

	commandList->CopyBufferRegion(buffer.Get(), 0, uploadBuffer.Get(), 0, bufferSize);

	CD3DX12_RESOURCE_BARRIER barrierCopyEnd = CD3DX12_RESOURCE_BARRIER::Transition(
		buffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE
	);
	commandList->ResourceBarrier(1, &barrierCopyEnd);

	m_commandQueue->ExecuteCommandList(commandList);
	m_commandQueue->Flush();
}

void Renderer::RenderShadowMaps(const Scene &scene, Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList)
{
	static D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, m_shadowMapSize, m_shadowMapSize, 0.0f, 1.0f);
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &m_scissorRect);

	// Transition depth buffer to RTV
	{
		if (m_directionalShadows.state != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				m_directionalShadows.depthBuffer.Get(),
				m_directionalShadows.state,
				D3D12_RESOURCE_STATE_DEPTH_WRITE
			);
			m_directionalShadows.state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			commandList->ResourceBarrier(1, &barrier);
		}
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		1, m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));
	commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	commandList->SetPipelineState(m_shadowMapPSO.Get());
	commandList->SetGraphicsRootSignature(m_rootSignature.Get());

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

	ID3D12DescriptorHeap *heaps[] = {
		m_resourceDescriptorHeap.Get()
	};
	commandList->SetDescriptorHeaps(1, heaps);

	commandList->SetGraphicsRootDescriptorTable(0, m_perObject->GetGPUDescriptorHandle());

	PerFrame perFrame;
	perFrame.view = scene.GetDirectionlLight().view;
	perFrame.proj = scene.GetDirectionlLight().proj;
	m_shadowPerFrame->Update(&perFrame, sizeof(PerFrame));
	commandList->SetGraphicsRootDescriptorTable(1, m_shadowPerFrame->GetGPUDescriptorHandle());

	for (const PerObject &instance : scene.GetCubeInstances()) {
		m_perObject->Update(&instance, sizeof(PerObject));
		commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
		commandList->IASetIndexBuffer(&m_indexBufferView);
		commandList->DrawIndexedInstanced(_countof(Cube::indices), 1, 0, 0, 0);
	}

	// Transition depth buffer to SRV for both PS and CS
	{
		if (m_directionalShadows.state != D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) {
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				m_directionalShadows.depthBuffer.Get(),
				m_directionalShadows.state,
				D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE
			);
			m_directionalShadows.state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
			commandList->ResourceBarrier(1, &barrier);
		}
	}
}
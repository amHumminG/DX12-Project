#include "Renderer.h"

#include "Helpers.h"
#include "Data.h"

#include <d3d12.h>
#include <DirectXMath.h>

Renderer::Renderer(std::unique_ptr<Window>* window)
{
	m_windowPtr = window;
}

bool Renderer::Initialize()
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
	m_RTVDescriptorHeap = CreateDescriptorHeap(m_device, m_numFrames + 1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_RTVDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	UpdateRenderTargetViews(m_device, m_swapChain, m_RTVDescriptorHeap);

	m_fov = 45.0f;

	if (!LoadContent()) return false;

	m_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(m_windowPtr->get()->GetWidth()), static_cast<float>(m_windowPtr->get()->GetHeight()));
	m_scissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

	m_modelMatrix = DirectX::XMMatrixIdentity();

	m_isInitialized = true;

	return true;
}

void Renderer::Render()
{
	if (!m_isInitialized) return;

	Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer = m_backBuffers[m_currentBackBufferIndex];

	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList = m_commandQueue->GetCommandList();

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		3, m_RTVDescriptorSize);
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();

	// Clear the render target
	{
		CD3DX12_RESOURCE_BARRIER barrier;
		if (m_sceneColorState != D3D12_RESOURCE_STATE_RENDER_TARGET)
		{
			barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_sceneColor.Get(),
				m_sceneColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
			m_sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

			commandList->ResourceBarrier(1, &barrier);
		}

		barrier = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer.Get(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		commandList->ResourceBarrier(1, &barrier);

		FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };

		commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

		commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}

	commandList->SetPipelineState(m_pipelineState.Get());
	commandList->SetGraphicsRootSignature(m_rootSignature.Get());

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	commandList->IASetIndexBuffer(&m_indexBufferView);

	commandList->RSSetViewports(1, &m_viewport);
	commandList->RSSetScissorRects(1, &m_scissorRect);

	commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

	ID3D12DescriptorHeap *heaps[] = {
		m_resourceDescriptorHeap.Get()
	};

	commandList->SetDescriptorHeaps(1, heaps);

	commandList->SetGraphicsRootDescriptorTable(0, m_constantBuffer->GetGPUDescriptorHandle());
	commandList->DrawIndexedInstanced(_countof(Cube::indices), 1, 0, 0, 0);

	{
		if (m_sceneColorState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_sceneColor.Get(),
				m_sceneColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			m_sceneColorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

			commandList->ResourceBarrier(1, &barrier);
		}

		rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			m_currentBackBufferIndex, m_RTVDescriptorSize);
		commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	}

	commandList->SetPipelineState(m_volFogPSO.Get());
	commandList->SetGraphicsRootDescriptorTable(1, m_cameraPS->GetGPUDescriptorHandle());
	commandList->SetGraphicsRootDescriptorTable(2, m_rayDataPS->GetGPUDescriptorHandle());
	CD3DX12_GPU_DESCRIPTOR_HANDLE srvTableHandle(m_resourceDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 3, m_resourceDescriptorSize);
	commandList->SetGraphicsRootDescriptorTable(3, srvTableHandle);
	commandList->DrawInstanced(3, 1, 0, 0);

	// Present
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

		commandList->ResourceBarrier(1, &barrier);

		ID3D12CommandList *const commandLists[] = { commandList.Get() };
		m_commandQueue->ExecuteCommandList(commandList);

		UINT syncInterval = m_vSync ? 1 : 0;
		UINT presentFlags = m_tearingSupported && !m_vSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
		ThrowIfFailed(m_swapChain->Present(syncInterval, presentFlags));

		uint64_t frameFenceValue = m_commandQueue->Signal();

		m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
		m_commandQueue->WaitForFenceValue(frameFenceValue);
	}
}

ConstantBuffer *Renderer::GetConstantBuffer()
{
	return m_constantBuffer.get();
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

void Renderer::UpdateRenderTargetViews(Microsoft::WRL::ComPtr<ID3D12Device2> device, Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain,
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap) {
	auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

	for (int i = 0; i < m_numFrames; i++) {
		Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;
		ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
		m_backBuffers[i] = backBuffer;
		rtvHandle.Offset(rtvDescriptorSize);
	}
}

bool Renderer::LoadContent()
{
	CreateRootSignature();

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

	// Create the descriptor heap for the depth-stencil view.
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DSVHeap)));

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

	auto fenceValue = m_commandQueue->ExecuteCommandList(commandList);
	m_commandQueue->WaitForFenceValue(fenceValue);

	m_contentLoaded = true;

	// Resize/Create the depth buffer.
	ResizeDepthBuffer(m_windowPtr->get()->GetWidth(), m_windowPtr->get()->GetHeight());

	return true;
}

void Renderer::CreateRootSignature()
{
	const unsigned int nResources = 11;
	m_resourceDescriptorHeap = CreateDescriptorHeap(m_device, nResources, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
	m_resourceDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	const unsigned int nParameters = 4;
	CD3DX12_ROOT_PARAMETER1 rootParameters[nParameters];
	CD3DX12_DESCRIPTOR_RANGE1 ranges[nParameters];
	CD3DX12_STATIC_SAMPLER_DESC staticSampler = {};

	// Vertex shader
	{
		// b0
		Cbuffer data = {};
		data.MVP = DirectX::XMMatrixIdentity();
		m_constantBuffer = std::make_unique<ConstantBuffer>(m_device, m_resourceDescriptorHeap, 0, &data, sizeof(Cbuffer));

		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);
	}

	// Pixel shader
	{
		// b0
		Camera camera;
		camera.position = { 1.0f, 0.0f, 0.0f };
		DirectX::XMStoreFloat4x4(&camera.viewProj, DirectX::XMMatrixIdentity());
		m_cameraPS = std::make_unique<ConstantBuffer>(m_device, m_resourceDescriptorHeap, 1, &camera, sizeof(Camera));

		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);

		// b1
		RayData rayData;
		rayData.totalSpotLights = 0;
		rayData.totalPointLights = 0;
		rayData.frameCount = 1;
		m_rayDataPS = std::make_unique<ConstantBuffer>(m_device, m_resourceDescriptorHeap, 2, &rayData, sizeof(RayData));

		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
		rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);

		// t0 - t7
		{
			ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0);
			rootParameters[3].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_PIXEL);

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

			// t0
			CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_resourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 3, m_resourceDescriptorSize);
			SetupVolumetricFogPS();
			m_device->CreateShaderResourceView(m_sceneColor.Get(), &srvDesc, hDescriptor);

			// t1
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			m_device->CreateShaderResourceView(nullptr, &srvDesc, hDescriptor);

			// t2
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			m_device->CreateShaderResourceView(nullptr, &sbViewDesc, hDescriptor);

			// t3
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			m_device->CreateShaderResourceView(nullptr, &arrayViewDesc, hDescriptor);

			// t4
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			m_device->CreateShaderResourceView(nullptr, &sbViewDesc, hDescriptor);

			// t5
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			m_device->CreateShaderResourceView(nullptr, &arrayViewDesc, hDescriptor);

			// t6
			hDescriptor.Offset(1, m_resourceDescriptorSize);
			m_device->CreateShaderResourceView(nullptr, &sbViewDesc, hDescriptor);

			// t7
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

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 3, m_RTVDescriptorSize);
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
			IID_PPV_ARGS(&m_depthBuffer)
		));

		// Update the depth-stencil view.
		D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
		dsv.Format = DXGI_FORMAT_D32_FLOAT;
		dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsv.Texture2D.MipSlice = 0;
		dsv.Flags = D3D12_DSV_FLAG_NONE;

		m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsv,
			m_DSVHeap->GetCPUDescriptorHandleForHeapStart());
	}
}

#include "Application.h"

#include <chrono>

static Application* g_appInstance = nullptr;

LRESULT CALLBACK GlobalWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (g_appInstance) {
		switch (message) {
			case WM_SIZE: {
				uint32_t width = LOWORD(lParam);
				uint32_t height = HIWORD(lParam);
				g_appInstance->OnResize(width, height);
				return 0;
			}
			case WM_KEYDOWN: {
				g_appInstance->OnKeyDown(static_cast<uint32_t>(wParam));
				return 0;
			}
			case WM_DESTROY: {
				g_appInstance->Shutdown();
				return 0;
			}
		}
	}

	return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

Application::Application(const wchar_t* title, uint32_t width, uint32_t height) {
	g_appInstance = this;
	m_window = std::make_unique<Window>(title, width, height, GlobalWndProc);
	m_renderer = std::make_unique<Renderer>(&m_window);
}

Application::~Application() {
	g_appInstance = nullptr;
}

bool Application::Initialize() {
	if (m_isInitialized) return true;

	if (!m_window->Initialize()) return false;

	// Initialize renderer here
	if (!m_renderer->Initialize()) return false;

	// Camera matrices
	{
		// Create the view matrix.
		const DirectX::XMVECTOR eyePosition = DirectX::XMVectorSet(0, 0, -10, 1);
		const DirectX::XMVECTOR focusPoint = DirectX::XMVectorSet(0, 0, 0, 1);
		const DirectX::XMVECTOR upDirection = DirectX::XMVectorSet(0, 1, 0, 0);
		m_camera.view = DirectX::XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);

		// Create the projection matrix.
		float fov = 45.0f;
		float aspectRatio = 1280 / static_cast<float>(720);
		m_camera.projection = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(fov), aspectRatio, 0.1f, 100.0f);
	}

	m_isInitialized = true;

	return true;
}

void Application::Run() {
	if (!m_isInitialized) return;

	m_isRunning = true;
	m_window->Show();

	auto t0 = std::chrono::high_resolution_clock::now();

	MSG msg = {};
	while (m_isRunning) {
		if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				m_isRunning = false;
			}
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
		else {
			auto t1 = std::chrono::high_resolution_clock::now();
			std::chrono::duration<double> deltaTime = t1 - t0;
			t0 = t1;

			Update(deltaTime.count());
			Render();
		}
	}
}

void Application::Shutdown() {
	m_isInitialized = false;
	m_isRunning = false;

	::PostQuitMessage(0);
}

void Application::OnResize(uint32_t width, uint32_t height) {
	m_window->SetSize(width, height);

	// Handle renderer swapchain resizing here
}

void Application::OnKeyDown(uint32_t key) {
	if (key == VK_ESCAPE) Shutdown();
}

void Application::Update(double deltaTime) {
	// Handle game logic
	// Update the model matrix.
	static double runTime = 0.0;
	runTime += deltaTime;
	float angle = static_cast<float>(90.0 * runTime);
	const DirectX::XMVECTOR rotationAxis = DirectX::XMVectorSet(0, 1, 1, 0);
	DirectX::XMMATRIX modelMatrix = DirectX::XMMatrixRotationAxis(rotationAxis, DirectX::XMConvertToRadians(angle));

	DirectX::XMMATRIX mvpMatrix = DirectX::XMMatrixMultiply(modelMatrix, m_camera.view);
	mvpMatrix = DirectX::XMMatrixMultiply(mvpMatrix, m_camera.projection);
	Cbuffer data = {};
	data.MVP = mvpMatrix;

	m_renderer->GetConstantBuffer()->Update(&data, sizeof(Cbuffer));
}

void Application::Render() {
	// Rendering logic
	m_renderer->Render();
}





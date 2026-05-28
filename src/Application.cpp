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

	m_scene.Initialize();

	if (!m_renderer->Initialize(m_scene)) return false;

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

	if (key == 'F') m_renderer->ToggleComputeShaderFog();

	if (key == '1') m_raySteps = 2;
	if (key == '2') m_raySteps = 4;
	if (key == '3') m_raySteps = 8;
	if (key == '4') m_raySteps = 16;
	if (key == '5') m_raySteps = 32;
	if (key == '6') m_raySteps = 64;
	if (key == '7') m_raySteps = 128;
}

void Application::Update(double deltaTime) {
	// Handle game logic
	// Update the model matrix.
	static long frameCount = 0;
	static double runTime = 0.0;
	runTime += deltaTime;

	m_scene.Update(deltaTime, runTime);

	RayData rayData;
	rayData.raySteps = m_raySteps;
	rayData.frameCount = frameCount;

	m_renderer->GetRayDataConstantBuffer()->Update(&rayData, sizeof(RayData));

	frameCount++;
}

void Application::Render() {
	// Rendering logic
	m_renderer->Render(m_scene);
}





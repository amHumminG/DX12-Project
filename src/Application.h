#pragma once

#include "Window.h"
#include "Renderer.h"

#include <memory>
#include <string>

class Application {
public:
	Application(const wchar_t* title, uint32_t width, uint32_t height);
	~Application();

	bool Initialize();
	void Run();
	void Shutdown();

	void OnResize(uint32_t width, uint32_t height);
	void OnKeyDown(uint32_t key);

private:
	void Update(double deltaTime);
	void Render();

	bool m_isInitialized = false;
	bool m_isRunning = false;

	std::unique_ptr<Window> m_window;
	std::unique_ptr<Renderer> m_renderer;

	struct Camera {
		DirectX::XMMATRIX view;
		DirectX::XMMATRIX projection;
	}m_camera;
};
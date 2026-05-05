#pragma once

// --- WINDOWS ---
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>

class Window {
public:
	Window(const wchar_t* title, uint32_t width, uint32_t height, WNDPROC wndProc);
	~Window();

	bool Initialize();

	void Show();
	void SetFullscreen(bool fullscreen);
	void SetSize(uint32_t width, uint32_t height);

	bool IsInitialized() const { return m_isInitialized; }
	HWND GetHWND() const { return m_hWnd; }
	uint32_t GetWidth() const { return m_width; }
	uint32_t GetHeight() const { return m_height; }
	bool IsFullScreen() const { return m_fullscreen; }

private:
	bool m_isInitialized = false;

	HWND m_hWnd; // Window handle
	RECT m_windowRect; // Window rectangle (Used to store window dimensions before going into fullscreen)
	WNDPROC m_wndProc;

	const wchar_t* m_title;
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	bool m_fullscreen = true;
};
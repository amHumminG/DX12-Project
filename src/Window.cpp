#include "Window.h"

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

#include <algorithm>

Window::Window(const wchar_t* title, uint32_t width, uint32_t height, WNDPROC wndProc)
	: m_hWnd(nullptr), m_windowRect{}, m_wndProc(wndProc),
	m_title(title), m_width(width), m_height(height) {
}

Window::~Window() {
	if (m_hWnd) ::DestroyWindow(m_hWnd);
}

bool Window::Initialize() {
	if (m_isInitialized) return true;

	// Get application instance handle
	HINSTANCE hInst = ::GetModuleHandle(NULL);
	const wchar_t* windowClassName = L"DX12WindowClass";

	// Register window class
	WNDCLASSEXW windowClass = {};
	windowClass.cbSize = sizeof(WNDCLASSEXW);
	windowClass.style = CS_HREDRAW | CS_VREDRAW; // Redraw window if size or position changes
	windowClass.lpfnWndProc = m_wndProc; // Callback passed from application
	windowClass.cbClsExtra = 0;
	windowClass.cbWndExtra = 0;
	windowClass.hInstance = hInst;
	windowClass.hIcon = ::LoadIcon(hInst, NULL); // Default icon
	windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW); // Default cursor
	windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	windowClass.lpszMenuName = NULL;
	windowClass.lpszClassName = windowClassName;
	windowClass.hIconSm = ::LoadIcon(hInst, NULL);

	static ATOM atom = ::RegisterClassExW(&windowClass);
	if (atom == 0) {
		::MessageBoxW(NULL, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
		return false;
	}

	// Get the actual window size depending on client area
	int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

	RECT windowRect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
	::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	// Center the window on the screen
	int windowWidth = windowRect.right - windowRect.left;
	int windowHeight = windowRect.bottom - windowRect.top;

	int windowX = std::max<int>(0, (screenWidth - windowWidth) / 2);
	int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);

	// Create the window
	m_hWnd = ::CreateWindowExW(
		NULL,
		windowClassName,
		m_title,
		WS_OVERLAPPEDWINDOW,
		windowX,
		windowY,
		windowWidth,
		windowHeight,
		NULL,
		NULL,
		hInst,
		nullptr
	);
	if (!m_hWnd) {
		::MessageBoxW(NULL, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
		return false;
	}

	::GetWindowRect(m_hWnd, &m_windowRect); // Save the starting

	m_isInitialized = true;
	return true;
}

void Window::Show() {
	if (!m_isInitialized) return;

	::ShowWindow(m_hWnd, SW_SHOW);
}

void Window::SetFullscreen(bool fullscreen) {
	if (!m_isInitialized) return;
	if (m_fullscreen == fullscreen) return;

	m_fullscreen = fullscreen;
	if (m_fullscreen) {
		// Store the current window rect
		::GetWindowRect(m_hWnd, &m_windowRect);

		// Remove window borders
		UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
		::SetWindowLongW(m_hWnd, GWL_STYLE, windowStyle);

		// Query the name of the nearest display device for the window (for multi-monitor setups)
		HMONITOR hMonitor = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFOEX monitorInfo = {};
		monitorInfo.cbSize = sizeof(MONITORINFOEX);
		::GetMonitorInfo(hMonitor, &monitorInfo);

		::SetWindowPos(m_hWnd, HWND_TOP,
			monitorInfo.rcMonitor.left,
			monitorInfo.rcMonitor.top,
			monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
			monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
			SWP_FRAMECHANGED | SWP_NOACTIVATE
		);

		::ShowWindow(m_hWnd, SW_MAXIMIZE);
	}
	else {
		// Restore all the window decorators
		::SetWindowLong(m_hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

		::SetWindowPos(m_hWnd, HWND_NOTOPMOST,
			m_windowRect.left,
			m_windowRect.top,
			m_windowRect.right - m_windowRect.left,
			m_windowRect.bottom - m_windowRect.top,
			SWP_FRAMECHANGED | SWP_NOACTIVATE);

		::ShowWindow(m_hWnd, SW_NORMAL);
	}
}

void Window::SetSize(uint32_t width, uint32_t height) {
	m_width = width;
	m_height = height;
}

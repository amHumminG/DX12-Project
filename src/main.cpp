#include "Application.h"

// Agility SDK Activation
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
	Application application(L"DX12 Project", 1280, 720);
	if (application.Initialize()) application.Run();

	return 0;
}

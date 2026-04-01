#include "DX12.h"

// Agility SDK
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    // Your DX12 initialization code will go here
    MessageBoxA(NULL, "DirectX 12 Sandbox", "Message", MB_OK);
    return 0;
}
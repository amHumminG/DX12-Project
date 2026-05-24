#pragma once

#include <DirectXMath.h>

struct VertexPosColor
{
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 color;
};

struct Cube {
	VertexPosColor vertices[8] = {
		{ DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f), DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f) }, // 0
		{ DirectX::XMFLOAT3(-1.0f,  1.0f, -1.0f), DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f) }, // 1
		{ DirectX::XMFLOAT3(1.0f,  1.0f, -1.0f), DirectX::XMFLOAT3(1.0f, 1.0f, 0.0f) }, // 2
		{ DirectX::XMFLOAT3(1.0f, -1.0f, -1.0f), DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f) }, // 3
		{ DirectX::XMFLOAT3(-1.0f, -1.0f,  1.0f), DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f) }, // 4
		{ DirectX::XMFLOAT3(-1.0f,  1.0f,  1.0f), DirectX::XMFLOAT3(0.0f, 1.0f, 1.0f) }, // 5
		{ DirectX::XMFLOAT3(1.0f,  1.0f,  1.0f), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f) }, // 6
		{ DirectX::XMFLOAT3(1.0f, -1.0f,  1.0f), DirectX::XMFLOAT3(1.0f, 0.0f, 1.0f) }  // 7
	};
	unsigned short indices[36] = {
		0, 1, 2, 0, 2, 3,
		4, 6, 5, 4, 7, 6,
		4, 5, 1, 4, 1, 0,
		3, 2, 6, 3, 6, 7,
		1, 5, 6, 1, 6, 2,
		4, 0, 3, 4, 3, 7
	};
	DirectX::XMMATRIX matrix = DirectX::XMMatrixIdentity();
};

struct Cbuffer {
	DirectX::XMMATRIX MVP;
};

struct Camera {
	DirectX::XMFLOAT3 position;
	float pad;
	DirectX::XMFLOAT4X4 viewProj;
};

struct RayData {
	unsigned int totalSpotLights;
	unsigned int totalPointLights;
	unsigned int frameCount;
	float pad;
};

struct DepthBuffer {
	Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer;

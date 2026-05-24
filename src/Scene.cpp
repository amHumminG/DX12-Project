#include "Scene.h"
#include "Scene.h"

void Scene::Initialize()
{
	// Camera
	{
		DirectX::XMFLOAT4 position = { 0.0f, 0.0f, -10.0f, 0.0f };
		// Create the view matrix.
		const DirectX::XMVECTOR eyePosition = DirectX::XMLoadFloat4(&position);
		const DirectX::XMVECTOR focusPoint = DirectX::XMVectorSet(0, 0, 0, 1);
		const DirectX::XMVECTOR upDirection = DirectX::XMVectorSet(0, 1, 0, 0);
		DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);
		view = DirectX::XMMatrixTranspose(view);
		DirectX::XMStoreFloat4x4(&m_camera.view, view);

		// Create the projection matrix.
		float fov = 45.0f;
		float aspectRatio = 1280 / static_cast<float>(720);
		DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(fov), aspectRatio, 0.1f, 100.0f);
		proj = DirectX::XMMatrixTranspose(proj);
		DirectX::XMStoreFloat4x4(&m_camera.proj, proj);
	}

	// Cubes
	{
		DirectX::XMMATRIX translation = DirectX::XMMatrixIdentity();
		PerObject instance;
		DirectX::XMStoreFloat4x4(&instance.model, translation);

		m_cubeInstances.push_back(instance);
	}
}

void Scene::Update(float deltaTime, float runTime)
{
	float angle = static_cast<float>(90.0 * runTime);
	const DirectX::XMVECTOR rotationAxis = DirectX::XMVectorSet(0, 1, 1, 0);
	DirectX::XMMATRIX modelMatrix = DirectX::XMMatrixRotationAxis(rotationAxis, DirectX::XMConvertToRadians(angle));
	modelMatrix = DirectX::XMMatrixTranspose(modelMatrix);
	DirectX::XMStoreFloat4x4(&m_cubeInstances[0].model, modelMatrix);
}

const PerFrame &Scene::GetCamera() const
{
	return m_camera;
}

const std::vector<PerObject> &Scene::GetCubeInstances() const
{
	return m_cubeInstances;
}

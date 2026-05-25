#include "Scene.h"

void Scene::Initialize()
{
	// Camera
	{
		m_position = { 0.0f, 0.0f, -10.0f };
		m_forward = { 0.0f, 0.0f, 1.0f };

		// Create the view matrix.
		const DirectX::XMVECTOR eyePosition = DirectX::XMLoadFloat3(&m_position);
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

	// Lights
	{
		// Directional
		{
			m_dirLight.color = { 1.0f, 0.8f, 0.6f };
			//m_dirLight.direction = { 4.0f, -8.0f, 2.0f };
			m_dirLight.direction = { 0.0f, -1.0f, 0.0f };

			DirectX::XMVECTOR eyePos = { 0.0f, 10.0f, 0.0f }; // Camera pos
			DirectX::XMVECTOR focusPos = { 0.0f, 0.0f, 0.0f }; // Camera direction
			DirectX::XMVECTOR upDir = { 0.0f, 0.0f, 1.0f }; // Up direction of camera
			DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eyePos, focusPos, upDir);
			view = DirectX::XMMatrixTranspose(view);
			DirectX::XMStoreFloat4x4(&m_dirLight.view, view);

			float width = 10.0f;
			float height = 10.0f;
			float nearZ = 0.1f;
			float farZ = 100.0f;
			DirectX::XMMATRIX orthographic = DirectX::XMMatrixOrthographicLH(width, height, nearZ, farZ);
			orthographic = DirectX::XMMatrixTranspose(orthographic);
			DirectX::XMStoreFloat4x4(&m_dirLight.proj, orthographic);
		}
	}
}

void Scene::Update(float deltaTime, float runTime)
{
	float angle = static_cast<float>(90.0 * runTime);
	const DirectX::XMVECTOR rotationAxis = DirectX::XMVectorSet(0, 1, 1, 0);
	DirectX::XMMATRIX modelMatrix = DirectX::XMMatrixRotationAxis(rotationAxis, DirectX::XMConvertToRadians(angle));
	modelMatrix = DirectX::XMMatrixTranspose(modelMatrix);
	DirectX::XMStoreFloat4x4(&m_cubeInstances[0].model, modelMatrix);

	if (GetAsyncKeyState('W') & 0x8000) {
		m_position.z += m_moveSpeed * deltaTime;
	}
	if (GetAsyncKeyState('S') & 0x8000) {
		m_position.z -= m_moveSpeed * deltaTime;
	}
	if (GetAsyncKeyState('D') & 0x8000) {
		m_position.x += m_moveSpeed * deltaTime;
	}
	if (GetAsyncKeyState('A') & 0x8000) {
		m_position.x -= m_moveSpeed * deltaTime;
	}
	if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
		m_position.y += m_moveSpeed * deltaTime;
	}
	if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) {
		m_position.y -= m_moveSpeed * deltaTime;
	}

	// Camera
	{
		// Create the view matrix.
		const DirectX::XMVECTOR eyePosition = DirectX::XMLoadFloat3(&m_position);
		const DirectX::XMVECTOR focusPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&m_position), DirectX::XMLoadFloat3(&m_forward));
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
}

const PerFrame &Scene::GetCamera() const
{
	return m_camera;
}

const std::vector<PerObject> &Scene::GetCubeInstances() const
{
	return m_cubeInstances;
}

DirectionalLight Scene::GetDirectionlLight() const
{
	return m_dirLight;
}

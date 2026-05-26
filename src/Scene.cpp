#include "Scene.h"

void Scene::Initialize()
{
	// Camera
	{
		m_position = { 0.0f, 0.0f, -10.0f };
		m_forward = { 0.0f, 0.0f, 1.0f };

		// Create the view matrix.
		UpdateCameraMatrices();
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
			m_dirLight.direction = { 4.0f, -8.0f, 2.0f };
			DirectX::XMVECTOR direction = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&m_dirLight.direction));
			DirectX::XMStoreFloat3(&m_dirLight.direction, direction);

			DirectX::XMVECTOR eyePos = DirectX::XMVectorMultiply(direction, { -10.0f, -10.0f, -10.0f }); // Camera pos
			DirectX::XMVECTOR focusPos = DirectX::XMVectorAdd(eyePos, direction); // Camera direction

			DirectX::XMVECTOR xAxis = { 1.0f, 0.0, 0.0f };
			DirectX::XMVECTOR cross = DirectX::XMVector3Cross(xAxis, direction);
			DirectX::XMVECTOR upDir = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(cross, direction)); // Up direction of camera
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
		UpdateCameraMatrices();
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

Camera Scene::GetCameraConstBuff() const
{
	// Create the view matrix.
	const DirectX::XMVECTOR eyePosition = { 0.0f, 0.0f, 0.0f };
	const DirectX::XMVECTOR focusPoint = DirectX::XMVectorAdd(eyePosition, DirectX::XMLoadFloat3(&m_forward));
	const DirectX::XMVECTOR upDirection = DirectX::XMVectorSet(0, 1, 0, 0);
	DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);
	view = DirectX::XMMatrixTranspose(view);

	// Create the projection matrix.
	float fov = 90.0f;
	float aspectRatio = 1280 / static_cast<float>(720);
	DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(fov), aspectRatio, 0.1f, 1000.0f);
	proj = DirectX::XMMatrixTranspose(proj);

	Camera camera;
	camera.position = m_position;
	DirectX::XMStoreFloat4x4(&camera.viewProj, DirectX::XMMatrixInverse(nullptr, DirectX::XMMatrixMultiply(view, proj)));

	return camera;
}

void Scene::UpdateCameraMatrices()
{
	// Create the view matrix.
	const DirectX::XMVECTOR eyePosition = DirectX::XMLoadFloat3(&m_position);
	const DirectX::XMVECTOR focusPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&m_position), DirectX::XMLoadFloat3(&m_forward));
	const DirectX::XMVECTOR upDirection = DirectX::XMVectorSet(0, 1, 0, 0);
	DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);
	view = DirectX::XMMatrixTranspose(view);
	DirectX::XMStoreFloat4x4(&m_camera.view, view);

	// Create the projection matrix.
	float fov = 90.0f;
	float aspectRatio = 1280 / static_cast<float>(720);
	DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(fov), aspectRatio, 0.1f, 1000.0f);
	proj = DirectX::XMMatrixTranspose(proj);
	DirectX::XMStoreFloat4x4(&m_camera.proj, proj);
}

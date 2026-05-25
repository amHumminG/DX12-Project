#pragma once

#include "Data.h"

#include <DirectXMath.h>
#include <vector>

class Scene
{
public:
	Scene() = default;
	~Scene() = default;

	void Initialize();

	void Update(float deltaTime, float runTime);

	const PerFrame &GetCamera() const;
	const std::vector<PerObject> &GetCubeInstances() const;

	DirectionalLight GetDirectionlLight() const;

private:
	DirectX::XMFLOAT3 m_position;
	DirectX::XMFLOAT3 m_forward;
	PerFrame m_camera;

	float m_moveSpeed = 10.0f;

	std::vector<PerObject> m_cubeInstances;

	DirectionalLight m_dirLight;

};
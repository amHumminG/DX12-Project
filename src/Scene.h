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

private:
	PerFrame m_camera;

	std::vector<PerObject> m_cubeInstances;

};
#pragma once
#define GLM_FORCE_QUAT_DATA_XYZW 1
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
class Camera
{
public:
	void Tick(float deltaTime
		, int forward, int left
		, float mouseX, float mouseY
		, float screenWidth, float screenHeight);
	glm::mat4 const& GetViewMatrix() const { return m_ViewMatrix; }
	glm::mat4 const& GetProjMatrix() const { return m_ProjectionMatrix; }
	glm::mat4 const& GetViewProjMatrix() const { return m_ViewProjectionMatrix; }
private:
	float m_FOV = 45.0f;
	glm::mat4 m_ViewMatrix;
	glm::mat4 m_ProjectionMatrix;
	glm::mat4 m_ViewProjectionMatrix;
	//Transform
	glm::vec3 m_Position = {0.0f, 0.0f, 0.0f};
	glm::vec2 m_Angles = {0.0f, 0.0f};
	glm::quat m_Rotation = glm::quat(0.0f, glm::vec3(0.0f, 1.0f, 0.0f));
};
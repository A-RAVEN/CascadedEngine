#include "Camera.h"
#include <iostream>


void Camera::Tick(float deltaTime
	, int forward, int left
	, float mouseX, float mouseY
	, float screenWidth, float screenHeight)
{
	m_Angles.x -= mouseX * 0.1f;
	m_Angles.y -= mouseY * 0.1f;
	m_Angles.x = glm::mod(m_Angles.x, 360.0f);
	m_Angles.y = glm::clamp(m_Angles.y, -90.0f, 90.0f);


	m_Rotation = glm::normalize( glm::quat(glm::radians(glm::vec3(m_Angles.y, 0.0f, m_Angles.x))));

	glm::vec3 forwardVec = m_Rotation * glm::vec3(0.0f, 1.0f, 0.0f);
	glm::vec3 leftVec = m_Rotation * glm::vec3(-1.0f, 0.0f, 0.0f);
	glm::vec3 upVec = m_Rotation * glm::vec3(0.0f, 0.0f, 1.0f);
	m_Position += (forwardVec * float(forward) + leftVec * float(left)) * 5.0f * deltaTime;
	m_ViewMatrix = glm::lookAt(m_Position, m_Position + forwardVec, upVec);
	m_ProjectionMatrix = glm::perspectiveRH_ZO(glm::radians(m_FOV), screenWidth / screenHeight, 0.1f, 1000.0f);
	m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
}

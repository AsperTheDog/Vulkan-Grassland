#include "camera.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include <SDL2/SDL_keycode.h>

Camera::Camera(const glm::vec3 pos, const glm::vec3 dir, const float fov, const float near, const float far)
	: m_position(pos), m_front(dir), m_fov(fov), m_near(near), m_far(far), m_viewDirty(true), m_projDirty(true)
{

}

void Camera::move(const glm::vec3 dir)
{
	m_position += dir;
	calculateRightVector();
	m_viewDirty = true;
}

void Camera::lookAt(const glm::vec3 target)
{
	m_front = glm::normalize(target - m_position);
	calculateRightVector();
	m_viewDirty = true;
}

void Camera::setPosition(const glm::vec3 pos)
{
	m_position = pos;
	calculateRightVector();
	m_viewDirty = true;
}

void Camera::setDir(const glm::vec3 dir)
{
	m_front = dir;
	calculateRightVector();
	m_viewDirty = true;
}

void Camera::setScreenSize(const uint32_t width, const uint32_t height)
{
	m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);
	m_projDirty = true;
}

void Camera::setProjectionData(const float fov, const float near, const float far)
{
	m_fov = fov;
	m_near = near;
	m_far = far;
	m_projDirty = true;
}

glm::vec3 Camera::getPosition() const
{
	return m_position;
}

glm::vec4 Camera::getPositionV4() const
{
	return {m_position, 1.0f};
}

glm::vec3 Camera::getDir() const
{
	return m_front;
}

glm::mat4& Camera::getViewMatrix()
{
	if (m_viewDirty)
	{
		m_viewMatrix = glm::lookAt(m_position, m_position + m_front, glm::vec3(0, 1, 0));
		m_viewDirty = false;
	}

	return m_viewMatrix;
}

glm::mat4& Camera::getProjMatrix()
{
	if (m_projDirty)
	{
		m_projMatrix = glm::perspective(glm::radians(m_fov), 16.0f/9, m_near, m_far);
		m_projDirty = false;
	}

	return m_projMatrix;
}

glm::mat4& Camera::getVPMatrix()
{
	if (m_projDirty || m_viewDirty)
	{
		m_VPMatrix = getProjMatrix() * getViewMatrix();
	}

	return m_VPMatrix;
}

void Camera::mouseMoved(const int32_t relX, const int32_t relY)
{
    if (!m_isMouseCaptured) return;
	m_yaw += static_cast<float>(relX) * m_mouseSensitivity;
    m_pitch += static_cast<float>(relY) * m_mouseSensitivity;

    if (m_pitch > 89.0f)
        m_pitch = 89.0f;
    if (m_pitch < -89.0f)
        m_pitch = -89.0f;
	if (m_yaw > 360.0f)
		m_yaw -= 360.0f;
	if (m_yaw < -360.0f)
		m_yaw += 360.0f;

    glm::vec3 newFront;
    newFront.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    newFront.y = sin(glm::radians(m_pitch));
    newFront.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    setDir(newFront);
}

void Camera::keyPressed(const uint32_t key)
{
    if (!m_isMouseCaptured)
    {
        m_wPressed = false;
        m_sPressed = false;
        m_aPressed = false;
        m_dPressed = false;
        m_spacePressed = false;
        m_shiftPressed = false;
        return;
    }
	switch (key)
	{
	case SDLK_w:
		m_wPressed = true;
		break;
	case SDLK_s:
		m_sPressed = true;
		break;
	case SDLK_a:
		m_aPressed = true;
		break;
	case SDLK_d:
		m_dPressed = true;
		break;
	case SDLK_SPACE:
		m_spacePressed = true;
		break;
	case SDLK_LSHIFT:
		m_shiftPressed = true;
		break;
	default:
		break;
	}
}

void Camera::keyReleased(const uint32_t key)
{
	switch (key)
	{
	case SDLK_w:
		m_wPressed = false;
		break;
	case SDLK_s:
		m_sPressed = false;
		break;
	case SDLK_a:
		m_aPressed = false;
		break;
	case SDLK_d:
		m_dPressed = false;
		break;
	case SDLK_SPACE:
		m_spacePressed = false;
		break;
	case SDLK_LSHIFT:
		m_shiftPressed = false;
		break;
	default:
		break;
	}
}

void Camera::updateEvents(const float delta)
{
	glm::vec3 moveDir(0.0f);
	if (m_wPressed)
	{
		moveDir += m_front;
	}
	if (m_sPressed)
	{
		moveDir -= m_front;
	}
	if (m_aPressed)
	{
		moveDir -= m_right;
	}
	if (m_dPressed)
	{
		moveDir += m_right;
	}
	if (m_spacePressed)
	{
		moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);
	}
	if (m_shiftPressed)
	{
		moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
	}

	if (moveDir.x != 0 || moveDir.y != 0 || moveDir.z != 0)
	{
		move(glm::normalize(moveDir) * (m_movingSpeed * delta));
	}
}

void Camera::calculateRightVector()
{
	m_right = glm::normalize(glm::cross(m_front, glm::vec3(0.0f, 1.0f, 0.0f)));
}

void Camera::setMouseCaptured(const bool captured)
{
    m_isMouseCaptured = captured;
}
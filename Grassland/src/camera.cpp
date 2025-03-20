#include "camera.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include "camera.hpp"
#include "camera.hpp"
#include "camera.hpp"
#include "camera.hpp"

#include <algorithm>
#include <glm/gtx/transform.hpp>

#include <SDL2/SDL_keycode.h>

Camera::Camera(const glm::vec3 pos, const glm::vec3 dir, const float fov, const float near, const float far)
	: m_Position(pos), m_Front(dir), m_fov(fov), m_near(near), m_far(far), m_viewDirty(true), m_projDirty(true)
{

}

void Camera::move(const glm::vec3 dir)
{
	m_Position += dir;
	calculateRightVector();
	setViewDirty();
}

void Camera::lookAt(const glm::vec3 target)
{
	m_Front = glm::normalize(target - m_Position);
	calculateRightVector();
	setViewDirty();
}

void Camera::setPosition(const glm::vec3 pos)
{
	m_Position = pos;
	calculateRightVector();
	setViewDirty();
}

void Camera::setDir(const glm::vec3 dir)
{
	m_Front = dir;
	calculateRightVector();
	setViewDirty();
}

void Camera::setScreenSize(const uint32_t width, const uint32_t height)
{
	m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);
	setProjDirty();
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
	return m_Position;
}

glm::vec4 Camera::getPositionV4() const
{
	return {m_Position, 1.0f};
}

glm::vec3 Camera::getDir() const
{
	return m_Front;
}

glm::vec2 Camera::getTiledPosition(const float p_TileSize) const
{
    glm::vec2 l_CameraTile = glm::vec2(m_Position.x, m_Position.z);
    l_CameraTile = glm::round(l_CameraTile / p_TileSize) * p_TileSize;

    return l_CameraTile;
}

glm::mat4& Camera::getViewMatrix()
{
	if (m_viewDirty)
	{
		m_viewMatrix = glm::lookAt(m_Position, m_Position + m_Front, glm::vec3(0, 1, 0));
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

void Camera::recalculateFrustum()
{
    getFrustum();
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
		moveDir += m_Front;
	}
	if (m_sPressed)
	{
		moveDir -= m_Front;
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

Camera::Frustum& Camera::getFrustum()
{
    if (!m_Frustum.frustumDirty) 
        return m_Frustum;

    // Updates VP matrix if dirty
    glm::mat4 l_VP = glm::transpose(getVPMatrix());

    // Extract planes from the VP matrix
    m_Frustum.planes[0] = l_VP[3] + l_VP[0]; // Left
    m_Frustum.planes[1] = l_VP[3] - l_VP[0]; // Right
    m_Frustum.planes[2] = l_VP[3] + l_VP[1]; // Bottom
    m_Frustum.planes[3] = l_VP[3] - l_VP[1]; // Top
    m_Frustum.planes[4] = l_VP[3] + l_VP[2]; // Near
    m_Frustum.planes[5] = l_VP[3] - l_VP[2]; // Far

    for (glm::vec4& plane : m_Frustum.planes)
    {
        glm::vec3 l_Normal = glm::vec3(plane);
        const float l_Length = glm::length(l_Normal);
        plane /= l_Length;
    }

    m_Frustum.frustumDirty = false;

    return m_Frustum;
}

void Camera::calculateRightVector()
{
	m_right = glm::normalize(glm::cross(m_Front, glm::vec3(0.0f, 1.0f, 0.0f)));
}

void Camera::setMouseCaptured(const bool captured)
{
    m_isMouseCaptured = captured;
}

void Camera::mouseScrolled(const int32_t y)
{
    // Change moving speed
    m_movingSpeed += y * 0.2f;
    m_movingSpeed = std::clamp(m_movingSpeed, 0.2f, 100.0f);
}

bool Camera::isBoxInFrustum(const glm::vec3& aabbMin, const glm::vec3& aabbMax)
{
    // Updates Frustum if dirty
    Frustum& l_Frustum = getFrustum();

    // Check if the box is in the frustum
    for (const glm::vec4& l_Plane : l_Frustum.planes)
    {
        glm::vec3 normal = glm::vec3(l_Plane); // Extract the normal from the plane equation

        // Compute the negative vertex (the point closest to the plane)
        glm::vec3 negative = aabbMin;

        if (normal.x >= 0.0f) negative.x = aabbMax.x;
        if (normal.y >= 0.0f) negative.y = aabbMax.y;
        if (normal.z >= 0.0f) negative.z = aabbMax.z;

        // If the negative vertex is outside the plane, the AABB is fully outside
        if (glm::dot(normal, negative) + l_Plane.w < 0.0f)
        {
            return false;
        }
    }
	return true;
}

void Camera::setViewDirty()
{
    m_viewDirty = true;
    m_Frustum.frustumDirty = true;
}

void Camera::setProjDirty()
{
    m_projDirty = true;
    m_Frustum.frustumDirty = true;
}

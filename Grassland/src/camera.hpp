#pragma once
#include <glm/glm.hpp>

#include "pp_fog_engine.hpp"

class Camera
{
public:
	struct Data
	{
		glm::vec4 position;
		glm::mat4 invPVMatrix;
	};

	Camera(glm::vec3 pos, glm::vec3 dir, float fov = 70.0f, float near = 0.1f, float far = 1000.0f);

	void move(glm::vec3 dir);
	void lookAt(glm::vec3 target);

	void setPosition(glm::vec3 pos);
	void setDir(glm::vec3 dir);

	void setScreenSize(uint32_t width, uint32_t height);
	void setProjectionData(float fov, float near, float far);

	[[nodiscard]] glm::vec3 getPosition() const;
	[[nodiscard]] glm::vec4 getPositionV4() const;
	[[nodiscard]] glm::vec3 getDir() const;

    [[nodiscard]] glm::vec2 getTiledPosition(float p_TileSize) const;

	glm::mat4& getViewMatrix();
	glm::mat4& getProjMatrix();
	glm::mat4& getVPMatrix();
	glm::mat4& getInvViewMatrix();
	glm::mat4& getInvProjMatrix();
    glm::mat4& getInvVPMatrix();
    void recalculateFrustum();
    [[nodiscard]] float getNearPlane() const { return m_near; }
    [[nodiscard]] float getFarPlane() const { return m_far; }

	void mouseMoved(int32_t relX, int32_t relY);
	void keyPressed(uint32_t key);
	void keyReleased(uint32_t key);
	void updateEvents(float delta);
    void setMouseCaptured(bool captured);
    void mouseScrolled(int32_t y);

    [[nodiscard]] bool isFrustumDirty() const { return m_Frustum.frustumDirty; }
    [[nodiscard]] bool isBoxInFrustum(const glm::vec3& aabbMin, const glm::vec3& aabbMax);

    void setViewDirty();
    void setProjDirty();

private:
    struct Frustum
    {
        glm::vec4 planes[6];
        bool frustumDirty = true;
    };
    Frustum& getFrustum();

	void calculateRightVector();

    float m_movingSpeed = 10.f;
	float m_mouseSensitivity = 0.1f;

	glm::vec3 m_Position;
	glm::vec3 m_Front;
	glm::vec3 m_right;
	float m_fov;
	float m_aspectRatio;
	float m_near;
	float m_far;

    float m_yaw;
    float m_pitch;

	bool m_viewDirty = true;
	glm::mat4 m_viewMatrix{};
	bool m_InvViewDirty = true;
	glm::mat4 m_invViewMatrix{};
	bool m_projDirty = true;
	glm::mat4 m_projMatrix{};
	bool m_InvProjDirty = true;
	glm::mat4 m_invProjMatrix{};

	glm::mat4 m_VPMatrix{};
    bool m_InvVPMatrixDirty = true;
    glm::mat4 m_InvVPMatrix{};
    Frustum m_Frustum;

	//Event tracker
	bool m_wPressed = false;
	bool m_aPressed = false;
	bool m_sPressed = false;
	bool m_dPressed = false;
	bool m_spacePressed = false;
	bool m_shiftPressed = false;
    bool m_isMouseCaptured = true;
};


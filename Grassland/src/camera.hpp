#pragma once
#include <glm/glm.hpp>

class Camera
{
public:
	struct Data
	{
		glm::vec4 position;
		glm::mat4 invPVMatrix;
	};

	Camera(glm::vec3 pos, glm::vec3 dir, float fov = 70.0f, float near = 0.01f, float far = 1000.0f);

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

	[[nodiscard]] Data getData();

	void mouseMoved(int32_t relX, int32_t relY);
	void keyPressed(uint32_t key);
	void keyReleased(uint32_t key);
	void updateEvents(float delta);
    void setMouseCaptured(bool captured);
    void mouseScrolled(int32_t y);

private:
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

	bool m_viewDirty;
	glm::mat4 m_viewMatrix{};
	bool m_projDirty;
	glm::mat4 m_projMatrix{};
	glm::mat4 m_VPMatrix{};

	//Event tracker
	bool m_wPressed = false;
	bool m_aPressed = false;
	bool m_sPressed = false;
	bool m_dPressed = false;
	bool m_spacePressed = false;
	bool m_shiftPressed = false;
    bool m_isMouseCaptured = true;
};


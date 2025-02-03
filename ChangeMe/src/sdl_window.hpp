#pragma once
#include <SDL2/SDL_vulkan.h>
#include <vector>
#include <SDL2/SDL_keycode.h>
#include <Volk/volk.h>

#include "utils/signal.hpp"

class VulkanFence;
class VulkanDevice;
class VulkanContext;
class VulkanQueue;

class SDLWindow
{
public:
	struct WindowSize
	{
		uint32_t width;
		uint32_t height;

		[[nodiscard]] VkExtent2D toExtent2D() const;
		WindowSize(uint32_t p_Width, uint32_t p_Height);
		WindowSize(Sint32 p_Width, Sint32 p_Height);
	};

	SDLWindow() = default;
	SDLWindow(std::string_view p_Name, int p_Width, int p_Height, int p_Top = SDL_WINDOWPOS_CENTERED, int p_Left = SDL_WINDOWPOS_CENTERED, uint32_t p_Flags = SDL_WINDOW_SHOWN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_RESIZABLE);


	[[nodiscard]] bool shouldClose() const;
	[[nodiscard]] std::vector<const char*> getRequiredVulkanExtensions() const;
	[[nodiscard]] WindowSize getSize() const;
    [[nodiscard]] bool isMinimized() const;

	void pollEvents();
	void toggleMouseCapture();

	void createSurface(VkInstance p_Instance);

	SDL_Window* operator*() const;
	[[nodiscard]] VkSurfaceKHR getSurface() const;

	void free();

	void initImgui() const;
	void frameImgui() const;
	void shutdownImgui() const;

	[[nodiscard]] Signal<VkExtent2D>& getResizedSignal();
	[[nodiscard]] Signal<int32_t, int32_t>& getMouseMovedSignal();
	[[nodiscard]] Signal<uint32_t>& getKeyPressedSignal();
	[[nodiscard]] Signal<uint32_t>& getKeyReleasedSignal();
	[[nodiscard]] Signal<float>& getEventsProcessedSignal();
    [[nodiscard]] Signal<bool>& getMouseCaptureChangedSignal();

private:

	SDL_Window* m_SDLHandle = nullptr;
	VkSurfaceKHR m_Surface = nullptr;

    VkInstance m_Instance = nullptr;

	// Signals
	Signal<VkExtent2D> m_ResizeSignal;      // WindowSize
	Signal<int32_t, int32_t> m_MouseMoved;  // relX, relY, isMouseCaptured
	Signal<uint32_t> m_KeyPressed;          // key, isMouseCaptured
	Signal<uint32_t> m_KeyReleased;         // key, isMouseCaptured
	Signal<float> m_EventsProcessed;        // delta
    Signal<bool> m_MouseCaptureChanged;     // isMouseCaptured

	float m_PrevDelta = 0.f;
	float m_Delta = 0.f;

	bool m_MouseCaptured = false;
    bool m_Minimized = false;

	friend class Surface;
	friend class VulkanGPU;
};


#pragma once
#include "client/Perlin.hpp"
#include "client/platform/AppPlatform.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

struct SDL_Window;
union SDL_Event;

class AppPlatform_SDL : public AppPlatform {
public:
    explicit AppPlatform_SDL(perlin::LaunchArgs args) : args(std::move(args)) {}

    bool Initialize() override;
    void Run() override;
    void Shutdown() override;

    NativeWindowInfo GetNativeWindowInfo() override;
    void GetFramebufferSize(int& width, int& height) override;
    void SetTitle(const std::string& title) override;
    void ShowSignInCode(const std::string& url, const std::string& code) override;
    bool ConfirmDialog(const std::string& title, const std::string& message) override;
    std::unique_ptr<perlin::RenderContext> CreateRenderContext() override;

private:
    void HandleEvent(const SDL_Event& event);
    void SetCursorCaptured(bool captured);
    bool cursorCaptured = false;

    SDL_Window* window = nullptr;
    NativeWindowInfo native;
    std::unique_ptr<perlin::Perlin> game;
    std::atomic<int> framebufferWidth{1280};
    std::atomic<int> framebufferHeight{720};
    std::mutex titleMutex;
    std::string pendingTitle;
    bool titleDirty = false;
    perlin::LaunchArgs args;
};

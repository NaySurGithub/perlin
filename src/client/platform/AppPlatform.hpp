#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace perlin {
    class RenderContext;
}

struct NativeWindowInfo {
    void* window = nullptr;
    void* display = nullptr;
    bool wayland = false;
};

class AppPlatform {
public:
    virtual ~AppPlatform() = default;

    virtual bool Initialize() = 0;
    virtual void Run() = 0;
    virtual void Shutdown() = 0;

    virtual NativeWindowInfo GetNativeWindowInfo() = 0;

    virtual void GetFramebufferSize(int& width, int& height) {
        width = 1280;
        height = 720;
    }

    virtual void SetTitle(const std::string& title) { (void)title; }

    virtual bool ConfirmDialog(const std::string& title, const std::string& message) {
        (void)title;
        (void)message;
        return false;
    }

    // Microsoft sign-in: open the verification page and put the code on the clipboard. Callable from any thread.
    virtual void ShowSignInCode(const std::string& url, const std::string& code) {
        (void)url;
        (void)code;
    }

    virtual std::unique_ptr<perlin::RenderContext> CreateRenderContext() = 0;

    std::atomic<bool> isRunning{true};
};

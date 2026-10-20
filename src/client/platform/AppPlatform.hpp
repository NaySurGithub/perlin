#pragma once
#include <mutex>
#include <vector>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace perlin {
    class RenderContext;
    struct TouchState;
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

    virtual void SetCursorCaptured(bool captured) { (void)captured; }

    virtual void PumpFrame() {}

    virtual void SetTitle(const std::string& title) { (void)title; }

    virtual bool ConfirmDialog(const std::string& title, const std::string& message) {
        (void)title;
        (void)message;
        return false;
    }

    virtual void ShowSignInCode(const std::string& url, const std::string& code) {
        (void)url;
        (void)code;
    }

    virtual void SetClipboard(const std::string& text) { (void)text; }
    virtual void OpenUrl(const std::string& url) { (void)url; }
    // Asks the user for a file to import ("skin", "cape" or "pack"); the chosen file arrives through
    // TakeDropped() like a dragged-in one. Returns false where the platform has no picker.
    // True once after the window's surface was replaced (Android: back from the background), so the renderer
    // rebuilds its swap chain.
    virtual bool TakeSurfaceChanged() { return false; }
    virtual bool PickFile(const std::string& kind) { (void)kind; return false; }

    virtual void SetTouchState(const perlin::TouchState& state) { (void)state; }
    virtual void DrawTouchOverlay(perlin::RenderContext& renderer) { (void)renderer; }

    virtual std::unique_ptr<perlin::RenderContext> CreateRenderContext() = 0;

    void PushDropped(const std::string& path) {
        std::lock_guard lock(dropMutex);
        droppedFiles.push_back(path);
    }
    std::vector<std::string> TakeDropped() {
        std::lock_guard lock(dropMutex);
        std::vector<std::string> out;
        out.swap(droppedFiles);
        return out;
    }

    std::atomic<bool> isRunning{true};

private:
    std::mutex dropMutex;
    std::vector<std::string> droppedFiles;
};

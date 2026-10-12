#pragma once
#include "client/Camera.hpp"
#include "client/Options.hpp"
#include "client/input/Input.hpp"
#include "client/input/InputManager.hpp"
#include "client/multiplayer/BedrockSession.hpp"
#include "client/player/LocalPlayer.hpp"
#include "client/renderer/LevelRenderer.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/RenderContext.hpp"
#include "util/ThreadPool.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class AppPlatform;

namespace perlin {
    struct LaunchArgs {
        std::string server;
        std::string playerName;
    };

    class Perlin {
    public:
        explicit Perlin(LaunchArgs args) : args(std::move(args)) {}

        bool Initialize(AppPlatform* platform);
        void Shutdown();

        Input& GetInput() { return input; }

    private:
        void RenderThreadMain();
        void Connect();
        bool AcceptEula();
        void TakeScreenshot();
        void UpdateTitle(int fps);

        LaunchArgs args;
        AppPlatform* platform = nullptr;
        Options options;
        Input input;
        InputManager inputManager{input, options};

        std::unique_ptr<RenderContext> renderer;
        std::unique_ptr<LevelRenderer> levelRenderer;
        std::unique_ptr<ThreadPool> workers;
        std::thread renderThread;
        std::atomic<bool> renderRunning{false};

        Camera camera;
        LocalPlayer player;
        BlockLightFlicker blockLight;
        double tickDebt = 0.0;
        float rainLevel = 0.0f;
        float thunderLevel = 0.0f;
        float previousRainLevel = 0.0f;
        float previousThunderLevel = 0.0f;

        std::unique_ptr<BedrockSession> session;
        std::atomic<bool> sessionReady{false};
    };
}

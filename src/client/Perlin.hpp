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
#include <mutex>
#include <string>
#include <thread>

class AppPlatform;

namespace perlin {
    struct LaunchArgs {
        std::string server;
        std::string playerName;
        std::string skin;       // --skin <png>
        std::string cape;       // --cape <png>
        bool slimArms = false;  // --slim
        bool login = false;     // --login: sign in with a Microsoft/Xbox account
        bool offline = false;   // --offline: ignore a saved online login for this run
        std::string transport;  // --raknet / --nethernet; empty = options.json (default auto)
        bool logout = false;    // --logout: forget the saved Microsoft login, then continue
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
        std::atomic<bool> renderReady{false};

        Camera camera;
        LocalPlayer player;
        BlockLightFlicker blockLight;
        double tickDebt = 0.0;
        float rainLevel = 0.0f;
        float thunderLevel = 0.0f;
        float previousRainLevel = 0.0f;
        float previousThunderLevel = 0.0f;

        std::unique_ptr<BedrockSession> session;
        std::shared_ptr<xbox::Account> account;
        std::mutex statusMutex;
        std::string statusHint; // shown in the window title: pending sign-in code, login or skin errors
        void SetStatusHint(const std::string& text);
        bool onlineThisRun = false;
        std::atomic<bool> sessionReady{false};
    };
}

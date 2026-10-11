#include "client/Perlin.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/Sky.hpp"
#include "client/resources/BedrockSamples.hpp"
#include "util/Paths.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>

namespace perlin {
    namespace {
        using Clock = std::chrono::steady_clock;

        constexpr float kMaxFrameDelta = 0.1f;
        constexpr float kDefaultFrameDelta = 0.016f;
        constexpr float kNoonTick = 6000.0f;
        constexpr auto kTitleInterval = std::chrono::milliseconds(500);
        constexpr auto kAnimationStep = std::chrono::milliseconds(200);

        bool ParseServer(const std::string& text, std::string& host, uint16_t& port) {
            host = text;
            port = 19132;
            const auto colon = text.rfind(':');
            if (colon == std::string::npos)
                return !host.empty();
            host = text.substr(0, colon);
            try {
                const int value = std::stoi(text.substr(colon + 1));
                if (value <= 0 || value > 65535)
                    return false;
                port = static_cast<uint16_t>(value);
            } catch (...) {
                return false;
            }
            return !host.empty();
        }

        std::string FormatClock(int64_t day, int tickOfDay) {
            const int hours = (tickOfDay / 1000 + 6) % 24;
            const int minutes = (tickOfDay % 1000) * 60 / 1000;
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "Day %lld %02d:%02d", static_cast<long long>(day + 1),
                          hours, minutes);
            return buffer;
        }
    }

    bool Perlin::AcceptEula() {
        if (options.eulaAccepted)
            return true;
        if (!platform->ConfirmDialog("Minecraft EULA",
                                     "This software uses Minecraft assets under the Mojang EULA.\n\n"
                                     "Do you accept the Minecraft EULA?"))
            return false;
        options.eulaAccepted = true;
        options.Save();
        return true;
    }

    bool Perlin::Initialize(AppPlatform* p) {
        platform = p;
        options = Options::Load();
        if (!args.server.empty())
            options.server = args.server;
        if (!args.playerName.empty())
            options.playerName = args.playerName;

        if (!AcceptEula())
            return false;

        renderer = platform->CreateRenderContext();
        if (!renderer)
            return false;

        workers = std::make_unique<ThreadPool>(2);
        workers->Submit([this] { BedrockSamples::Ensure(options.bedrockSamplesDir); });
        Connect();

        renderRunning = true;
        renderThread = std::thread(&Perlin::RenderThreadMain, this);
        return true;
    }

    void Perlin::Connect() {
        std::string host;
        uint16_t port = 0;
        if (!ParseServer(options.server, host, port)) {
            std::cout << "[perlin] invalid server address '" << options.server << "'" << std::endl;
            return;
        }

        session = std::make_unique<BedrockSession>();
        session->SetPlayerName(options.playerName);
        session->SetChunkRadius(options.renderDistance);
        workers->Submit([this, host, port] {
            if (session->Connect(host, port))
                sessionReady = true;
        });
    }

    void Perlin::Shutdown() {
        renderRunning = false;
        if (renderThread.joinable())
            renderThread.join();
        if (session)
            session->Disconnect();
        if (workers)
            workers->Stop();
        options.Save();
    }

    void Perlin::TakeScreenshot() {
        const std::string dir = paths::DataDir() + "/screenshots";
        paths::EnsureDir(dir);
        const std::time_t now = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H.%M.%S", std::localtime(&now));
        renderer->RequestScreenshot(dir + "/" + stamp + ".bmp");
    }

    void Perlin::UpdateTitle(int fps) {
        std::string title = "Perlin | " + std::to_string(fps) + " FPS";
        int64_t day = 0;
        int tick = 0;
        if (sessionReady && session->GetWorldTime(day, tick))
            title += " | " + FormatClock(day, tick);
        if (levelRenderer)
            title += " | " + std::to_string(levelRenderer->MeshedChunks()) + " chunks";
        platform->SetTitle(title);
    }

    void Perlin::RenderThreadMain() {
        if (!renderer->Initialize(*platform, options)) {
            std::cout << "[render] renderer initialisation failed" << std::endl;
            platform->isRunning = false;
            return;
        }
        levelRenderer = std::make_unique<LevelRenderer>(*renderer, options);

        camera.fov = options.fov;
        camera.farPlane = std::max(256.0f, (options.renderDistance + 2) * 16.0f * 1.5f);

        auto previous = Clock::now();
        const auto startedAt = previous;
        uint64_t frameCounter = 0;
        auto titleAt = previous;
        auto animationAt = previous;
        uint64_t animationStep = 0;
        int frames = 0;

        while (renderRunning && platform->isRunning) {
            const auto frameStart = Clock::now();
            float dt = std::chrono::duration<float>(frameStart - previous).count();
            previous = frameStart;
            if (dt <= 0.0f || dt > kMaxFrameDelta)
                dt = kDefaultFrameDelta;

            inputManager.Poll();
            if (inputManager.Pressed(Action::CyclePerspective))
                camera.CycleMode();
            const LookDelta look = inputManager.Look();
            camera.Rotate(look.yaw, look.pitch);

            int width = 0, height = 0;
            platform->GetFramebufferSize(width, height);
            if (width > 0 && height > 0)
                camera.aspect = static_cast<float>(width) / static_cast<float>(height);

            BedrockSession* live = sessionReady ? session.get() : nullptr;
            if (live) {
                live->Update();
                live->PumpRequests(64);
                player.Update(*live, inputManager, camera, levelRenderer->Blocks(), dt);
            }
            levelRenderer->ReloadAssetsIfNeeded();

            float tickOfDay = kNoonTick;
            int64_t day = 0;
            int tick = 0;
            if (live && live->GetWorldTime(day, tick))
                tickOfDay = static_cast<float>(tick);
            SkyState sky = ComputeSky(SkyProfile::Overworld(), tickOfDay);
            BlendFogTowardSky(sky, static_cast<float>(options.renderDistance));
            const FogRange fog = TerrainFog(static_cast<float>(options.renderDistance) * 16.0f);

            const glm::mat4 viewProjection = camera.ViewProjection();
            const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
            const Frustum frustum = Frustum::FromViewProjection(viewProjection);
            const glm::vec3 eye = camera.EyePosition();

            FrameParams params{};
            std::copy_n(glm::value_ptr(viewProjection), 16, params.viewProjection);
            std::copy_n(glm::value_ptr(inverseViewProjection), 16, params.inverseViewProjection);
            std::copy_n(glm::value_ptr(eye), 3, params.cameraPosition);
            params.renderDistance = static_cast<float>(options.renderDistance) * 16.0f;
            std::copy_n(sky.skyColor, 3, params.skyColor);
            std::copy_n(sky.fogColor, 3, params.fogColor);
            std::copy_n(sky.sunDirection, 3, params.sunDirection);
            std::copy_n(sky.sunriseColor, 4, params.sunriseColor);
            params.fogStart = fog.start;
            params.fogEnd = fog.end;
            params.celestialAngle = sky.celestialAngle;
            params.skyDarken = sky.skyDarken;
            params.starBrightness = sky.starBrightness;
            params.timeSeconds = std::chrono::duration<float>(frameStart - startedAt).count();
            params.frameIndex = static_cast<uint32_t>(frameCounter++);
            BuildLightmap({sky.skyDarken, options.gamma, SkyProfile::Overworld().ambientLight, 0.0f},
                          params.lightmap);

            renderer->BeginFrame(params);
            if (live) {
                levelRenderer->Update(&live->NetLevel(), camera.x, camera.z, frustum);
                levelRenderer->Render(frustum, camera.x, camera.z);
            }
            if (inputManager.Pressed(Action::Screenshot))
                TakeScreenshot();
            renderer->EndFrame();

            if (frameStart - animationAt >= kAnimationStep) {
                animationAt = frameStart;
                levelRenderer->TickAnimation(++animationStep);
            }

            frames++;
            const auto now = Clock::now();
            if (now - titleAt >= kTitleInterval) {
                const double seconds = std::chrono::duration<double>(now - titleAt).count();
                UpdateTitle(static_cast<int>(frames / seconds + 0.5));
                titleAt = now;
                frames = 0;
            }

            if (!options.vsync && options.maxFps > 0) {
                const auto target = frameStart + std::chrono::microseconds(1000000 / options.maxFps);
                std::this_thread::sleep_until(target);
            }
        }

        levelRenderer.reset();
        renderer->Shutdown();
    }
}

#include "client/Perlin.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/Environment.hpp"
#include "client/renderer/Fog.hpp"
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
        constexpr float kHorizonHeight = 63.0f;
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

        float StepWeather(float level, float target) {
            if (level < target)
                return std::min(target, level + 0.01f);
            return std::max(target, level - 0.01f);
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
        camera.nearPlane = 0.05f;
        camera.farPlane = std::max(static_cast<float>(options.renderDistance * 16 * 4),
                                   static_cast<float>(GraphicsSettings::From(options).cloudRange * 16));

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
            camera.fov = options.fov * (options.fovEffects ? player.FovModifier() : 1.0f);

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

            double worldTicks = kNoonTick;
            if (live)
                live->GetWorldTicks(worldTicks);

            tickDebt += dt * 20.0f;
            while (tickDebt >= 1.0) {
                tickDebt -= 1.0;
                blockLight.Tick();
                if (live) {
                    previousRainLevel = rainLevel;
                    previousThunderLevel = thunderLevel;
                    rainLevel = StepWeather(rainLevel, live->RainTarget());
                    thunderLevel = StepWeather(thunderLevel, live->ThunderTarget());
                }
            }
            const float partial = static_cast<float>(tickDebt);
            const float rain = glm::mix(previousRainLevel, rainLevel, partial);
            const float thunder = glm::mix(previousThunderLevel, thunderLevel, partial) * rain;

            EnvironmentInput input;
            input.ticks = worldTicks;
            input.rain = rain;
            input.thunder = thunder;
            if (live)
                input.dimension = static_cast<Dimension>(std::clamp(live->DimensionId(), 0, 2));
            const Environment env = SampleEnvironment(input);

            const glm::mat4 viewProjection = camera.ViewProjection();
            const Frustum frustum = Frustum::FromViewProjection(viewProjection);
            const glm::vec3 eye = camera.EyePosition();
            const GraphicsSettings graphics = GraphicsSettings::From(options);

            FogInput fogInput;
            fogInput.renderDistanceChunks = options.renderDistance;
            fogInput.cloudRangeChunks = graphics.cloudRange;
            fogInput.forward = camera.Direction();
            fogInput.cameraY = eye.y;
            fogInput.rain = rain;
            fogInput.thunder = thunder;
            const FogState fog = ComputeFog(env, fogInput);

            FrameParams params{};
            std::copy_n(glm::value_ptr(viewProjection), 16, params.viewProjection);
            std::copy_n(glm::value_ptr(camera.SkyViewProjection()), 16, params.skyViewProjection);
            std::copy_n(glm::value_ptr(eye), 3, params.cameraPosition);
            std::copy_n(glm::value_ptr(fog.color), 4, params.fogColor);
            params.environmentalFogStart = fog.environmentalStart;
            params.environmentalFogEnd = fog.environmentalEnd;
            params.renderDistanceFogStart = fog.renderDistanceStart;
            params.renderDistanceFogEnd = fog.renderDistanceEnd;
            params.skyFogEnd = fog.skyEnd;
            params.cloudFogEnd = fog.cloudEnd;
            params.rgss = graphics.rgss;

            params.sky.enabled = input.dimension == Dimension::Overworld;
            std::copy_n(glm::value_ptr(env.skyColor), 3, params.sky.skyColor);
            params.sky.sunAngle = env.sunAngle;
            params.sky.moonAngle = env.moonAngle;
            params.sky.starAngle = env.starAngle;
            params.sky.rainBrightness = 1.0f - rain;
            params.sky.starBrightness = env.starBrightness;
            std::copy_n(glm::value_ptr(env.sunriseColor), 4, params.sky.sunrise);
            params.sky.moonPhase = env.moonPhase;
            params.sky.darkDisc = eye.y < kHorizonHeight;

            const glm::vec4 cloudColor = glm::round(glm::clamp(env.cloudColor, 0.0f, 1.0f) * 255.0f) / 255.0f;
            params.cloud.enabled = graphics.clouds && cloudColor.w > 0.0f;
            params.cloud.fancy = graphics.fancyClouds;
            params.cloud.rangeChunks = graphics.cloudRange;
            std::copy_n(glm::value_ptr(cloudColor), 4, params.cloud.color);
            params.cloud.height = env.cloudHeight;
            params.cloud.gameTicks = std::chrono::duration<double>(frameStart - startedAt).count() * 20.0;

            params.timeSeconds = std::chrono::duration<float>(frameStart - startedAt).count();
            params.frameIndex = static_cast<uint32_t>(frameCounter++);

            LightmapParams lightmap;
            lightmap.blockFactor = blockLight.Value() + 1.4f;
            lightmap.skyFactor = env.skyLightFactor;
            lightmap.skyLightColor = env.skyLightColor;
            lightmap.ambientColor = env.ambientColor;
            lightmap.blockLightTint = env.blockLightTint;
            lightmap.nightVisionColor = env.nightVisionColor;
            lightmap.brightness = options.gamma;
            BuildLightmap(lightmap, params.lightmap);

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

#include "client/Zoom.hpp"
#include "client/Perlin.hpp"
#include "client/multiplayer/SkinLoader.hpp"
#include "network/XboxAuth.hpp"
#include "client/renderer/chunk/ChunkMesher.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/Environment.hpp"
#include "client/renderer/Fog.hpp"
#include "client/player/BlockCollision.hpp"
#include "client/player/DamageEffect.hpp"
#include "client/PreviewWorld.hpp"
#include "client/renderer/chunk/LodStore.hpp"
#include "client/resources/BedrockSamples.hpp"
#include "util/Paths.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
        if (args.logout) {
            xbox::Account::SignOut(paths::DataDir() + "/auth.json");
            options.onlineLogin = false;
            std::cout << "[auth] saved Microsoft login removed" << std::endl;
        }
        if (args.login)
            options.onlineLogin = true; // remembered: the next start signs in again without the flag
        onlineThisRun = options.onlineLogin && !args.offline;

        if (!AcceptEula())
            return false;

        renderer = platform->CreateRenderContext();
        if (!renderer)
            return false;

        workers = std::make_unique<ThreadPool>(2);
        workers->Submit([this] { BedrockSamples::Ensure(options.bedrockSamplesDir); });
        if (!std::getenv("PERLIN_PREVIEW"))
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
        // Ask the server for the whole render distance. Within 32 chunks columns are full (meshed); beyond that only the
        // surface is fetched and sampled into the far-terrain (LOD) store.
        session->SetChunkRadius(options.renderDistance);
        session->SetMaxViewDistance(std::max(32, options.renderDistance));
        session->SetNearRadius(options.renderDistance > LodStore::kRealChunks ? LodStore::kRealChunks : 1 << 20);

        // --skin / --cape / --slim override options.json for this run only.
        SkinConfig skinConfig;
        skinConfig.skinPath = args.skin.empty() ? options.skin : args.skin;
        skinConfig.capePath = args.cape.empty() ? options.cape : args.cape;
        skinConfig.slimArms = args.slimArms || options.slimArms;
        LoadedSkin loadedSkin;
        std::string skinError;
        if (!LoadSkinConfig(skinConfig, loadedSkin, skinError)) {
            std::cout << "[perlin] skin: " << skinError << " - using the default skin" << std::endl;
            SetStatusHint("Skin not loaded: " + skinError);
            skinConfig.skinPath.clear();
            skinConfig.capePath.clear();
            LoadSkinConfig(skinConfig, loadedSkin, skinError);
        } else if (loadedSkin.hasSkin() || loadedSkin.hasCape()) {
            std::cout << "[perlin] skin " << (loadedSkin.hasSkin() ? skinConfig.skinPath : std::string("(default)"))
                      << (loadedSkin.hasCape() ? ", cape " + skinConfig.capePath : std::string())
                      << (loadedSkin.slimArms ? ", slim arms" : "") << std::endl;
        }
        session->SetSkin(std::move(loadedSkin));

        if (onlineThisRun) {
            account = std::make_shared<xbox::Account>(paths::DataDir() + "/auth.json");
            session->SetAccount(account);
        }
        workers->Submit([this, host, port] {
            if (account) {
                std::string error;
                const bool signedIn = account->SignIn(
                    [this](const std::string& url, const std::string& code) {
                        std::cout << "[auth] To sign in, open " << url << " and enter the code " << code << std::endl;
                        platform->ShowSignInCode(url, code);
                        SetStatusHint("Sign in at " + url + " with code " + code + " (copied to the clipboard)");
                    },
                    error);
                if (!signedIn) {
                    std::cout << "[auth] sign-in failed: " << error << std::endl;
                    SetStatusHint("Sign-in failed: " + error);
                    return;
                }
                SetStatusHint({});
            }
            if (session->Connect(host, port))
                sessionReady = true;
            else if (account)
                SetStatusHint("Login failed: " + session->LastError());
        });
    }

    void Perlin::SetStatusHint(const std::string& text) {
        std::lock_guard lock(statusMutex);
        statusHint = text;
    }

    void Perlin::Shutdown() {
        renderRunning = false;
        if (renderThread.joinable())
            renderThread.join();
        if (account)
            account->Cancel(); // a pending sign-in would otherwise keep the worker busy for minutes
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
        std::string title = "Perlin";
        if (renderer && renderReady) {
            const std::string backend = renderer->RendererName();
            if (!backend.empty())
                title += " (" + backend + ")";
        }
        title += " | " + std::to_string(fps) + " FPS";
        int64_t day = 0;
        int tick = 0;
        if (sessionReady && session->GetWorldTime(day, tick))
            title += " | " + FormatClock(day, tick);
        if (levelRenderer)
            title += " | " + std::to_string(levelRenderer->MeshedChunks()) + " chunks";
        {
            std::lock_guard lock(statusMutex);
            if (!statusHint.empty())
                title += " | " + statusHint;
        }
        platform->SetTitle(title);
    }

    void Perlin::RenderThreadMain() {
        if (!renderer->Initialize(*platform, options)) {
            std::cout << "[render] renderer initialisation failed" << std::endl;
            platform->isRunning = false;
            return;
        }
        renderReady = true;
        levelRenderer = std::make_unique<LevelRenderer>(*renderer, options);

        camera.fov = options.fov;
        camera.nearPlane = 0.05f;
        camera.farPlane = std::max(static_cast<float>(options.renderDistance * 16 * 4),
                                   static_cast<float>(GraphicsSettings::From(options).cloudRange * 16));

        Zoom zoom(options.zoom);
        auto previous = Clock::now();
        const auto startedAt = previous;
        uint64_t frameCounter = 0;
        DamageEffect damage;
        uint32_t lastHurtCount = 0, lastHurtSerial = 0;
        auto titleAt = previous;
        auto animationAt = previous;
        uint64_t animationStep = 0;

        // Developer preview (PERLIN_PREVIEW=1): generated world, fixed camera/time, optional screenshot and exit.
        const bool preview = std::getenv("PERLIN_PREVIEW") != nullptr;
        std::unique_ptr<Level> previewLevel;
        double previewTicks = kNoonTick;
        std::string previewShot;
        double previewWait = 15.0;
        int previewShotFrames = -1;
        auto previewReadyAt = previous;
        size_t previewLastMeshed = 0;
        if (preview) {
            camera.x = 24.0f; camera.y = 74.0f; camera.z = -22.0f; camera.yaw = 100.0f; camera.pitch = -6.0f;
            if (const char* c = std::getenv("PERLIN_PREVIEW_CAM"))
                std::sscanf(c, "%f,%f,%f,%f,%f", &camera.x, &camera.y, &camera.z, &camera.yaw, &camera.pitch);
            if (const char* t = std::getenv("PERLIN_PREVIEW_TIME"))
                previewTicks = std::atof(t);
            if (const char* t = std::getenv("PERLIN_PREVIEW_SHOT"))
                previewShot = t;
            if (const char* t = std::getenv("PERLIN_PREVIEW_WAIT"))
                previewWait = std::atof(t);
        }
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
            const LookDelta look = zoom.Update(dt, inputManager.IsDown(Action::Zoom), inputManager.Scroll(),
                                               inputManager.Look());
            camera.Rotate(look.yaw, look.pitch);
            camera.fov = zoom.Fov(options.fov * (options.fovEffects ? player.FovModifier() : 1.0f));
            if (sessionReady) {
                const uint32_t hurts = session->HurtCount();
                const auto source = session->LatestHurtSource();
                // Java: hurtDir = bearing to the damage source - the player's yaw (random 0/180 with no source).
                const auto bearing = [&] {
                    if (!source.known)
                        return static_cast<float>(std::rand() % 2) * 180.0f;
                    const float javaYaw = camera.yaw - 90.0f; // Perlin yaw 90 looks +Z, Java yaw 0 does
                    return static_cast<float>(std::atan2(source.dz, source.dx) * 57.29577951308232) - javaYaw;
                };
                if (hurts != lastHurtCount) {
                    lastHurtCount = hurts;
                    lastHurtSerial = source.serial;
                    damage.Hurt(bearing());
                } else if (source.serial != lastHurtSerial) {
                    lastHurtSerial = source.serial;
                    if (source.known)
                        damage.Redirect(bearing());
                }
            } else {
                damage = DamageEffect();
            }

            int width = 0, height = 0;
            platform->GetFramebufferSize(width, height);
            if (width > 0 && height > 0)
                camera.aspect = static_cast<float>(width) / static_cast<float>(height);

            BedrockSession* live = sessionReady ? session.get() : nullptr;
            if (live) {
                live->Update();
                live->PumpRequests(256);
                player.Update(*live, inputManager, camera, levelRenderer->Blocks(), dt);
            }
            levelRenderer->ReloadAssetsIfNeeded();

            double worldTicks = preview ? previewTicks : kNoonTick;
            if (live)
                live->GetWorldTicks(worldTicks);
            if (preview && !previewLevel) {
                static auto lastNote = frameStart;
                if (frameStart - lastNote > std::chrono::seconds(3)) {
                    lastNote = frameStart;
                    std::cout << "[preview] waiting: ready=" << levelRenderer->Blocks().Ready()
                              << " complete=" << levelRenderer->Blocks().Complete() << std::endl;
                }
            }
            if (preview && !previewLevel && levelRenderer->Blocks().Ready()) {
                auto built = std::make_unique<Level>();
                if (BuildPreviewWorld(*built, 7)) {
                    previewLevel = std::move(built);
                    previewReadyAt = frameStart;
                    std::cout << "[preview] world built" << std::endl;
                }
            }

            // Camera inside water: fog, colour and water vision follow Java's WaterFogEnvironment.
            bool underwater = false;
            std::string eyeBiome = "minecraft:plains";
            if (live) {
                const glm::vec3 e = camera.EyePosition();
                const int bx = static_cast<int>(std::floor(e.x)), by = static_cast<int>(std::floor(e.y)),
                          bz = static_cast<int>(std::floor(e.z));
                const LevelCollision col(live->NetLevel(), &levelRenderer->Blocks());
                const physics::BlockShape cell = col.Query(bx, by, bz);
                if (cell.fluid == 1) {
                    const float top = col.Query(bx, by + 1, bz).fluid == 1 ? 1.0f : cell.fluidLevel;
                    underwater = e.y < static_cast<float>(by) + top;
                }
                if (underwater)
                    eyeBiome = levelRenderer->Tints().NameForId(live->NetLevel().GetBiome(bx, by, bz));
            }
            static int waterVisionTime = 0;

            tickDebt += dt * 20.0f;
            while (tickDebt >= 1.0) {
                tickDebt -= 1.0;
                blockLight.Tick();
                damage.Tick();
                waterVisionTime = underwater ? std::min(600, waterVisionTime + 1) : std::max(0, waterVisionTime - 10);
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

            camera.viewEffect = sessionReady ? damage.ViewMatrix(partial, options.damageTilt) : glm::mat4(1.0f);
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
            fogInput.underwater = underwater;
            if (underwater) {
                const float t = static_cast<float>(waterVisionTime);
                fogInput.waterVision = waterVisionTime >= 600
                                           ? 1.0f
                                           : std::clamp(t / 100.0f, 0.0f, 1.0f) * 0.6f +
                                                 std::clamp((t - 100.0f) / 500.0f, 0.0f, 1.0f) * 0.39999998f;
                struct WaterFog { const char* biome; uint32_t rgb; };
                static const WaterFog kWaterFog[] = {
                    {"cherry_grove", 0x5db7ef}, {"dappled_forest", 0x375154}, {"deep_lukewarm_ocean", 0x041633},
                    {"lukewarm_ocean", 0x041633}, {"mangrove_swamp", 0x4d7a60}, {"pale_garden", 0x556980},
                    {"sulfur_caves", 0x17543c}, {"swamp", 0x232317}, {"warm_ocean", 0x041f33}};
                uint32_t rgb = 0x050533;
                std::string shortName = eyeBiome.rfind("minecraft:", 0) == 0 ? eyeBiome.substr(10) : eyeBiome;
                for (const WaterFog& w : kWaterFog)
                    if (shortName == w.biome)
                        rgb = w.rgb;
                fogInput.waterFogColor = {((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f};
                if (shortName == "swamp")
                    fogInput.waterFogEndScale = 0.85f;
            }
            const FogState fog = ComputeFog(env, fogInput);

            if (preview) {
                static int once = 0;
                if (once++ == 200)
                    std::cout << "[preview] fog env " << fog.environmentalStart << ".." << fog.environmentalEnd
                              << " rd " << fog.renderDistanceStart << ".." << fog.renderDistanceEnd << " sky "
                              << fog.skyEnd << " rdchunks " << options.renderDistance << " ticks " << worldTicks << std::endl;
            }
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

            params.crosshair = true;
            params.screenTint[0] = 1.0f;
            params.screenTint[1] = 0.0f;
            params.screenTint[2] = 0.0f;
            params.screenTint[3] = options.damageTint ? damage.TintAlpha() : 0.0f; // vanilla overlay: constant while hurt

            {
                static float waterTile = -1, flowTile = -1, lavaTile = -1;
                static int tileScan = 0;
                if ((tileScan++ % 120) == 0 && levelRenderer->Blocks().Ready()) {
                    waterTile = static_cast<float>(levelRenderer->Blocks().TileContaining("water_still"));
                    flowTile = static_cast<float>(levelRenderer->Blocks().TileContaining("water_flow"));
                    lavaTile = static_cast<float>(levelRenderer->Blocks().TileContaining("lava_still"));
                }
                params.waterTile = waterTile;
                params.waterFlowTile = flowTile;
                params.lavaTile = lavaTile;
                params.heldLight = static_cast<float>(options.heldLight);
                if (const char* h = std::getenv("PERLIN_HELDLIGHT"))
                    params.heldLight = static_cast<float>(std::atoi(h));
            }
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
            } else if (previewLevel) {
                levelRenderer->Update(previewLevel.get(), camera.x, camera.z, frustum);
                levelRenderer->Render(frustum, camera.x, camera.z);
                if (!previewShot.empty() && previewShotFrames < 0) {
                    // Wait until meshing has settled, then capture.
                    const size_t meshed = levelRenderer->MeshedChunks();
                    if (meshed != previewLastMeshed) {
                        previewLastMeshed = meshed;
                        previewReadyAt = frameStart;
                    }
                    if (std::chrono::duration<double>(frameStart - previewReadyAt).count() > previewWait) {
                        renderer->RequestScreenshot(previewShot);
                        previewShotFrames = 0;
                    }
                } else if (previewShotFrames >= 0 && ++previewShotFrames > 6) {
                    std::cout << "[preview] screenshot saved, meshed chunks " << levelRenderer->MeshedChunks() << std::endl;
                    MeshProfilePrint();
                    platform->isRunning = false;
                }
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

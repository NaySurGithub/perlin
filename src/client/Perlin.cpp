#include "client/SampleProfiler.hpp"
#include "util/Env.hpp"
#include "client/Zoom.hpp"
#include "client/Perlin.hpp"
#include "client/i18n/Lang.hpp"
#include "client/multiplayer/SkinLoader.hpp"
#include "network/XboxAuth.hpp"
#include "client/renderer/chunk/ChunkMesher.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/DynamicLights.hpp"
#include "client/input/Keys.hpp"
#include "client/renderer/Environment.hpp"
#include "client/renderer/Fog.hpp"
#include "client/player/BlockCollision.hpp"
#include "client/player/BlockDestroy.hpp"
#include "client/player/BlockUse.hpp"
#include "client/ui/DeathScreen.hpp"
#include "client/ui/InventoryScreen.hpp"
#include "client/ui/DisconnectScreen.hpp"
#include "client/ui/UiLayout.hpp"
#include "client/ui/PauseScreen.hpp"
#include "client/ui/UiTemplate.hpp"
#include "client/player/DamageEffect.hpp"
#include "client/PreviewWorld.hpp"
#include "client/renderer/chunk/LodStore.hpp"
#include "client/resources/BedrockSamples.hpp"
#include "world/level/lighting/ChunkLighting.hpp"
#include "client/ui/DebugScreen.hpp"
#include "client/ui/GuiScale.hpp"
#include "client/ui/ItemData.hpp"
#include "util/SystemInfo.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "client/ui/UiAssets.hpp"
#include "util/Paths.hpp"

#include "client/player/Interaction.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <ctime>
#include <iostream>

namespace perlin {
    namespace {
        uint8_t CachedLight(Level& level, int bx, int by, int bz, std::chrono::steady_clock::time_point now) {
            struct Band {
                ChunkLighting::Result lit;
                std::chrono::steady_clock::time_point at{};
                uint64_t counter = 0;
            };
            static std::unordered_map<uint64_t, Band> bands;
            static const Level* owner = nullptr;
            if (owner != &level || bands.size() > 256) {
                bands.clear();
                owner = &level;
            }
            const int cx = bx >> 4, cz = bz >> 4, band = by >> 4;
            const uint64_t key = (static_cast<uint64_t>(cx & 0xFFFFF) << 28) | (static_cast<uint64_t>(cz & 0xFFFFF) << 8) |
                                 static_cast<uint64_t>((band + 128) & 0xFF);
            Band& b = bands[key];
            const uint64_t counter = level.changeCounter.load(std::memory_order_relaxed);
            const auto age = now - b.at;
            if (b.at == std::chrono::steady_clock::time_point{} ||
                (counter != b.counter && age > std::chrono::milliseconds(400)) || age > std::chrono::seconds(20)) {
                b.at = now;
                b.counter = counter;
                ChunkLighting::Compute(level, {cx, cz}, band * 16 - 8, band * 16 + 23, b.lit);
            }
            return b.lit.At(bx & 15, by, bz & 15);
        }
    }

    namespace {
        struct FrameProfile {
            static constexpr int kCount = 10;
            double ms[kCount] = {};
            int frames = 0;
            bool enabled = PERLIN_ENV("PERLIN_PROFILE") != nullptr;
            const char* names[kCount] = {"logic", "level update", "level render", "entities", "hand", "hud", "end frame", "frame", "", ""};
        } gProfile;
        struct ProfScope {
            int id;
            std::chrono::steady_clock::time_point start;
            explicit ProfScope(int i) : id(i) {
                if (gProfile.enabled)
                    start = std::chrono::steady_clock::now();
            }
            ~ProfScope() {
                if (gProfile.enabled)
                    gProfile.ms[id] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            }
        };
    }

    namespace {
        AimHit gAim;
        bool gItemSlow = false;
        struct LocalCrack { bool active = false; int x = 0, y = 0, z = 0, stage = 0; };
        LocalCrack gLocalCrack;
        bool showHitboxes = false;
        bool showChunkBorders = false;
        bool advancedTooltips = false;
        bool softPaused = false;
        int switcherIndex = -1;
    }
}

namespace perlin {
    namespace {
        using Clock = std::chrono::steady_clock;
        uint64_t MsNow() {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
        }

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
        i18n::Lang::Get().Configure(std::string(), paths::DataDir(), options.language);
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
            options.onlineLogin = true;
        onlineThisRun = options.onlineLogin && !args.offline;

        if (!AcceptEula())
            return false;

        renderer = platform->CreateRenderContext();
        if (!renderer)
            return false;

        workers = std::make_unique<ThreadPool>(2);
        workers->Submit([this] { BedrockSamples::Ensure(options.bedrockSamplesDir); });
        if (!PERLIN_ENV("PERLIN_PREVIEW"))
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

        particleLevel = nullptr;
        session = std::make_unique<BedrockSession>();
        session->SetPlayerName(options.playerName);
        session->SetTransport(args.transport.empty() ? options.transport : args.transport);
        session->SetChunkRadius(options.renderDistance);
        session->SetMaxViewDistance(std::max(32, options.renderDistance));
        session->SetNearRadius(options.renderDistance > LodStore::kRealChunks ? LodStore::kRealChunks : 1 << 20);

        SkinConfig skinConfig;
        skinConfig.skinPath = args.skin.empty() ? options.skin : args.skin;
        skinConfig.capePath = args.cape.empty() ? options.cape : args.cape;
        skinConfig.slimArms = args.slimArms || options.slimArms;
        LoadedSkin loadedSkin;
        std::string skinError;
        if (!LoadSkinConfig(skinConfig, loadedSkin, skinError)) {
            std::cout << "[perlin] skin: " << skinError << " - using the default skin" << std::endl;
            SetStatusHint(i18n::Tr("perlin.status.skin_failed", {skinError}));
            skinConfig.skinPath.clear();
            skinConfig.capePath.clear();
            LoadSkinConfig(skinConfig, loadedSkin, skinError);
        } else if (loadedSkin.hasSkin() || loadedSkin.hasCape()) {
            std::cout << "[perlin] skin " << (loadedSkin.hasSkin() ? skinConfig.skinPath : std::string("(default)"))
                      << (loadedSkin.hasCape() ? ", cape " + skinConfig.capePath : std::string())
                      << (loadedSkin.slimArms ? ", slim arms" : "") << std::endl;
        }
        session->SetSkin(std::move(loadedSkin));
        session->SetPackDirectory(paths::DataDir() + "/server_packs");

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
                        SetStatusHint(i18n::Tr("perlin.status.sign_in", {url, code}));
                    },
                    error);
                if (!signedIn) {
                    std::cout << "[auth] sign-in failed: " << error << std::endl;
                    SetStatusHint(i18n::Tr("perlin.status.sign_in_failed", {error}));
                    return;
                }
                SetStatusHint({});
            }
            if (session->Connect(host, port))
                sessionReady = true;
            else if (account)
                SetStatusHint(i18n::Tr("perlin.status.login_failed", {session->LastError()}));
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
            account->Cancel();
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

    namespace {
        void RayTargets(Level& level, glm::vec3 origin, glm::vec3 dir, double reach, ui::DebugInfo& info) {
            BlockPalette& palette = BlockPalette::Instance();
            static std::unordered_map<uint16_t, int> kind;
            const auto classify = [&](uint16_t id) {
                const auto it = kind.find(id);
                if (it != kind.end())
                    return it->second;
                const std::string name = palette.Name(id);
                int k = 2;
                if (name == "minecraft:air" || name == "minecraft:cave_air" || name == "minecraft:void_air" || name.empty())
                    k = 0;
                else if (name == "minecraft:water" || name == "minecraft:flowing_water" || name == "minecraft:lava" ||
                         name == "minecraft:flowing_lava")
                    k = 1;
                return kind[id] = k;
            };
            const auto describe = [&](ui::DebugInfo::Target& t, int x, int y, int z, uint16_t id) {
                t.valid = true;
                t.x = x;
                t.y = y;
                t.z = z;
                t.name = palette.Name(id);
                for (const auto& [key, value] : palette.States(id)) {
                    std::string k = key;
                    if (k.rfind("minecraft:", 0) == 0)
                        k.erase(0, 10);
                    std::string v;
                    switch (value.kind) {
                    case BlockPalette::StateValue::Bool: v = value.i ? "true" : "false"; break;
                    case BlockPalette::StateValue::Int: v = std::to_string(value.i); break;
                    default: v = value.s; break;
                    }
                    t.properties.emplace_back(k, v);
                }
            };
            int x = static_cast<int>(std::floor(origin.x)), y = static_cast<int>(std::floor(origin.y)),
                z = static_cast<int>(std::floor(origin.z));
            const int stepX = dir.x > 0 ? 1 : -1, stepY = dir.y > 0 ? 1 : -1, stepZ = dir.z > 0 ? 1 : -1;
            const auto inv = [](float d) { return d != 0.0f ? std::fabs(1.0f / d) : 1e30f; };
            const float dx = inv(dir.x), dy = inv(dir.y), dz = inv(dir.z);
            const auto firstBoundary = [](float o, int cell, int step, float delta) {
                return (step > 0 ? (static_cast<float>(cell) + 1.0f - o) : (o - static_cast<float>(cell))) * delta;
            };
            float tx = firstBoundary(origin.x, x, stepX, dx), ty = firstBoundary(origin.y, y, stepY, dy),
                  tz = firstBoundary(origin.z, z, stepZ, dz);
            float t = 0.0f;
            while (t <= reach) {
                bool loaded = true;
                const uint16_t id = level.GetBlock(x, y, z, loaded);
                if (!loaded)
                    return;
                const int k = classify(id);
                if (k == 2) {
                    describe(info.block, x, y, z, id);
                    return;
                }
                if (k == 1 && !info.fluid.valid)
                    describe(info.fluid, x, y, z, id);
                if (tx < ty && tx < tz) {
                    t = tx;
                    tx += dx;
                    x += stepX;
                } else if (ty < tz) {
                    t = ty;
                    ty += dy;
                    y += stepY;
                } else {
                    t = tz;
                    tz += dz;
                    z += stepZ;
                }
            }
        }
    }

    void Perlin::UpdateDeathScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_DEATH") != nullptr;
        bool dead = preview || (live && state.mode != ui::GameMode::Spectator && (state.health <= 0.0f || live->DeathPending()));
        if (live && live->DeathPending() && state.health > 0.0f && deathShown && Clock::now() - deathAt > std::chrono::seconds(2))
            live->ClearDeath();
        const auto now = Clock::now();
        if (dead && deathRespawnSent) {
            if (now - deathSentAt < std::chrono::seconds(3))
                dead = false;
            else
                deathRespawnSent = false;
        } else if (!dead) {
            deathRespawnSent = false;
        }
        if (!dead) {
            if (deathShown) {
                hud.Overlay().ClearOverlay();
                deathShown = false;
                deathSignature.clear();
            }
            if (deathTextMode) {
                deathTextMode = false;
                if (!chat.IsOpen())
                    input.SetTextMode(false);
            }
            deathLeftDown = false;
            return;
        }
        if (!deathShown) {
            deathShown = true;
            deathAt = now;
            std::cout << "[ui] death screen shown: health=" << state.health << " pending=" << (live && live->DeathPending())
                      << " cause='" << (live ? live->DeathCause() : std::string()) << "' hud=" << state.visible << std::endl;
        }
        if (!deathTextMode) {
            deathTextMode = true;
            input.SetTextMode(true);
        }

        const bool buttons = now - deathAt >= std::chrono::seconds(1);
        ui::DeathScreenText t;
        t.title = i18n::Tr("perlin.death.title");
        t.cause = live ? live->DeathCause() : std::string();
        t.respawn = i18n::Tr("perlin.death.respawn");
        t.titleScreen = i18n::Tr("perlin.death.title_screen");
        t.buttonsEnabled = buttons;

        if (now - deathCheckAt > std::chrono::milliseconds(400) || deathTemplate.empty()) {
            deathCheckAt = now;
            uint64_t version = 0;
            std::string text = ui::LoadDeathScreenTemplate(paths::DataDir(), version);
            if (version != deathTemplateVersion || deathTemplate.empty()) {
                deathTemplateVersion = version;
                deathTemplate = std::move(text);
                deathSignature.clear();
                std::cout << "[ui] death screen: loaded " << paths::DataDir() << "/ui/death_screen.rml" << std::endl;
            }
        }

        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        const std::string signature = t.title + "|" + t.cause + "|" + std::to_string(guiW) + "x" + std::to_string(guiH) + "|" +
                                      (buttons ? "1" : "0") + "|" + t.respawn + t.titleScreen;
        if (signature != deathSignature || !hud.Overlay().HasOverlay()) {
            deathSignature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildDeathScreenRml(deathTemplate, t, guiW, guiH), error)) {
                std::cout << "[ui] death_screen.rml: " << error << std::endl;
                deathTemplate = ui::DefaultDeathScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildDeathScreenRml(deathTemplate, t, guiW, guiH), error);
            }
        }
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (PERLIN_ENV("PERLIN_PREVIEW_MOUSE") && !deathHoverLogged) {
            deathHoverLogged = true;
            std::cout << "[ui] death screen hover: '" << hud.Overlay().OverlayHover() << "'" << std::endl;
        }
        if (!down && deathLeftDown && buttons && !preview) {
            const std::string hover = hud.Overlay().OverlayHover();
            std::cout << "[ui] death screen click on '" << hover << "'" << std::endl;
            if (hover == ui::kDeathRespawnId && live) {
                live->RequestRespawn();
                deathRespawnSent = true;
                deathSentAt = now;
            }
        }
        deathLeftDown = down;
    }

    void Perlin::FillDebug(ui::HudState& s, BedrockSession* live, Level* level) {
        s.debug = debugOn && s.visible;
        if (!s.debug)
            return;
        const auto now = Clock::now();
        if (live && now - rateAt >= std::chrono::seconds(1)) {
            txRate = static_cast<int>(live->PacketsSent() - rateTx);
            rxRate = static_cast<int>(live->PacketsReceived() - rateRx);
            rateTx = live->PacketsSent();
            rateRx = live->PacketsReceived();
            rateAt = now;
        }
        ui::DebugInfo d;
        d.version = std::string("Perlin ") + "1.0.0" + " (bedrock " + kBedrockVersion + "/vanilla)";
        d.fps = fpsShown;
        d.maxFps = options.vsync ? 0 : options.maxFps;
        d.vsync = options.vsync;
        d.fancyGraphics = options.fancyGraphics;
        const GraphicsSettings gfx = GraphicsSettings::From(options);
        d.clouds = gfx.clouds;
        d.fancyClouds = gfx.fancyClouds;
        d.biomeBlend = gfx.biomeBlendRadius;
        d.tx = txRate;
        d.rx = rxRate;
        d.renderDistance = options.renderDistance;
        d.simulationDistance = options.renderDistance;
        if (levelRenderer) {
            d.chunksRendered = static_cast<int>(levelRenderer->MeshedChunks());
            d.pendingChunks = static_cast<int>(levelRenderer->PendingMeshes());
            d.pendingUploads = static_cast<int>(levelRenderer->MeshesInFlight());
            d.availableBuffers = static_cast<int>(levelRenderer->MaxMeshesInFlight() - std::min(levelRenderer->MaxMeshesInFlight(), levelRenderer->MeshesInFlight()));
        }
        if (level) {
            std::lock_guard lock(level->mutex);
            d.chunksLoaded = static_cast<int>(level->chunks.size());
        }
        static const char* kDimensions[] = {"minecraft:overworld", "minecraft:the_nether", "minecraft:the_end"};
        if (live)
            d.dimension = kDimensions[std::clamp(live->DimensionId(), 0, 2)];

        const glm::vec3 eye = camera.EyePosition();
        d.x = camera.x;
        d.y = camera.y - 1.62;
        d.z = camera.z;
        d.yaw = camera.yaw - 90.0f;
        d.pitch = -camera.pitch;
        d.skyLight = dbgSky;
        d.blockLight = dbgBlock;
        d.compiler = sysinfo::Compiler();
        d.memUsed = sysinfo::ProcessMemory();
        d.memPeak = sysinfo::ProcessPeakMemory();
        d.memTotal = sysinfo::TotalMemory();
        d.cpuThreads = sysinfo::CpuThreads();
        static const std::string cpu = sysinfo::CpuName();
        d.cpuName = cpu;
        int w = 0, h = 0;
        platform->GetFramebufferSize(w, h);
        d.displayW = w;
        d.displayH = h;
        if (renderer) {
            d.gpuLine1 = renderer->RendererName();
            d.displayVendor = renderer->GpuVendor();
            d.gpuLine2 = renderer->GpuDescription();
        }
        if (level) {
            const int bx = static_cast<int>(std::floor(d.x)), bz = static_cast<int>(std::floor(d.z));
            bool loaded = false;
            level->GetBlock(bx, static_cast<int>(std::floor(d.y)), bz, loaded);
            d.inLoadedChunk = loaded;
            if (loaded) {
                if (now - heightAt > std::chrono::milliseconds(500)) {
                    heightAt = now;
                    BlockPalette& palette = BlockPalette::Instance();
                    heightSurface = level->FindBlockBelow(bx, bz, 319, [&](uint16_t id) { return palette.Name(id) != "minecraft:air"; }) + 1;
                }
                d.heightWorldSurface = d.heightMotionBlocking = heightSurface;
                d.biome = levelRenderer ? levelRenderer->Tints().NameForId(level->GetBiome(bx, static_cast<int>(std::floor(d.y)), bz))
                                        : "minecraft:plains";
                if (d.biome.rfind("minecraft:", 0) != 0)
                    d.biome = "minecraft:" + d.biome;
            }
            RayTargets(*level, eye, camera.Direction(), 20.0, d);
        }
        ui::BuildDebugLines(d, s.debugLeft, s.debugRight);
    }

    void Perlin::UpdateAudio(BedrockSession* live, double now, float dt) {
        static bool opened = false, hooked = false;
        const std::string& root = ui::UiAssets::Instance().VanillaRoot();
        if (!opened && (live != nullptr || PERLIN_ENV("PERLIN_AUDIO"))) {
            opened = true;
            if (!audioEngine.Open())
                std::cout << "[audio] running without an output device" << std::endl;
        }
        if (!hooked) {
            hooked = true;
            sounds.SetBlockNamer([](int32_t runtimeId) {
                BlockPalette& palette = BlockPalette::Instance();
                return palette.Name(palette.Intern(runtimeId));
            });
            sounds.SetSubtitleHook([this](const std::string& sound, const glm::dvec3& position, float volume, bool global) {
                if (options.sound.subtitles)
                    subtitles.Add(sound, position, volume, global, static_cast<double>(std::chrono::duration<double>(
                                                                         std::chrono::steady_clock::now().time_since_epoch()).count()));
            });
        }
        if (!root.empty())
            sounds.Configure(root);
        audio::Volumes volumes;
        const SoundSettings& v = options.sound;
        volumes.v = {v.master, v.music, v.record, v.weather, v.block, v.hostile, v.neutral, v.player, v.ambient, v.voice};
        audioEngine.SetVolumes(volumes);
        audioEngine.SetListener({camera.x, camera.y, camera.z}, camera.yaw - 90.0f);
        sounds.Tick(now);

        if (live) {
            for (const BedrockSession::SoundRequest& r : live->TakeSounds()) {
                const glm::dvec3 at(r.x, r.y, r.z);
                switch (r.kind) {
                case BedrockSession::SoundRequest::Kind::Named:
                    sounds.PlayNamed(r.name, at, r.volume > 0.0f ? r.volume : 1.0f, r.pitch > 0.0f ? r.pitch : 1.0f, !r.global);
                    break;
                case BedrockSession::SoundRequest::Kind::Level:
                    sounds.PlayLevelSound(r.name, at, r.extra, r.entityType, r.baby, r.global);
                    break;
                case BedrockSession::SoundRequest::Kind::Stop:
                    if (r.stopAll)
                        audioEngine.StopAll();
                    else
                        sounds.Stop(r.name);
                    break;
                }
            }

            const physics::State& st = player.State();
            static physics::State last = st;
            static double walked = 0, nextStep = 1;
            static bool haveLast = false;
            if (!haveLast) {
                last = st;
                haveLast = true;
            }
            Level& level = live->NetLevel();
            const auto blockUnder = [&]() -> std::string {
                for (double dy : {0.2, 0.6, 1.1}) {
                    bool loaded = false;
                    const uint16_t id = level.GetBlock(static_cast<int>(std::floor(st.x)), static_cast<int>(std::floor(st.y - dy)),
                                                       static_cast<int>(std::floor(st.z)), loaded);
                    if (!loaded)
                        continue;
                    const std::string name = BlockPalette::Instance().Name(id);
                    if (name != "minecraft:air" && !sounds.BlockSoundType(name).empty())
                        return name;
                }
                return {};
            };
            const glm::dvec3 feet(st.x, st.y, st.z);
            if (hudState.mode != ui::GameMode::Spectator) {
                static double swimClock = 0;
                swimClock += dt;
                if (st.inWater && !last.inWater && std::fabs(st.vy) > 0.1)
                    sounds.PlayEntityEvent("minecraft:player", "splash", feet);
                else if (st.inWater && swimClock > 0.45 && std::hypot(st.x - last.x, st.z - last.z) > 0.03) {
                    swimClock = 0;
                    sounds.PlayEntityEvent("minecraft:player", "swim", feet);
                }
                if (st.onGround && !last.onGround && !st.inWater) {
                    const std::string under = blockUnder();
                    if (!under.empty())
                        sounds.PlayBlockEvent(under, last.y - st.y > 3.0 ? "fall" : "land", feet);
                } else if (!st.onGround && last.onGround && st.y > last.y && !st.inWater) {
                    const std::string under = blockUnder();
                    if (!under.empty())
                        sounds.PlayBlockEvent(under, "jump", feet);
                }
                if (st.onGround && !st.inWater && !st.onClimbable && !player.Sneaking()) {
                    walked += std::hypot(st.x - last.x, st.z - last.z) * 0.6;
                    if (walked > nextStep) {
                        nextStep = std::floor(walked) + 1.0;
                        const std::string under = blockUnder();
                        if (!under.empty())
                            sounds.PlayBlockEvent(under, "step", feet);
                    }
                }
            }
            last = st;
        }

        hudState.subtitles.clear();
        if (options.sound.subtitles) {
            const double clockNow = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            for (const audio::Subtitles::Line& l : subtitles.Visible({camera.x, camera.y, camera.z}, camera.yaw - 90.0f, clockNow))
                hudState.subtitles.push_back({l.text, l.direction, l.brightness});
        } else {
            subtitles.Clear();
        }
        if (PERLIN_ENV("PERLIN_PREVIEW_BORDERS"))
            showChunkBorders = true;
        if (PERLIN_ENV("PERLIN_PREVIEW_HITBOXES"))
            showHitboxes = true;
        if (PERLIN_ENV("PERLIN_PREVIEW_SUBTITLES")) {
            hudState.subtitles = {{"Cow moos", -1, 1.0f}, {"Footsteps", 0, 0.8f}, {"Zombie groans", 1, 0.5f}, {"Explosion", 1, 0.3f}};
        }
        (void)dt;
        (void)now;
    }

    void Perlin::UpdateTitle(int fps) {
        std::string title = "Perlin";
        if (renderer && renderReady) {
            const std::string backend = renderer->RendererName();
            if (!backend.empty())
                title += " (" + backend + ")";
        }
        title += " | " + i18n::Tr("perlin.title.fps", {std::to_string(fps)});
        int64_t day = 0;
        int tick = 0;
        if (sessionReady && session->GetWorldTime(day, tick))
            title += " | " + FormatClock(day, tick);
        if (levelRenderer)
            title += " | " + i18n::Tr("perlin.title.chunks", {std::to_string(levelRenderer->MeshedChunks())});
        {
            std::lock_guard lock(statusMutex);
            if (!statusHint.empty())
                title += " | " + statusHint;
        }
        platform->SetTitle(title);
    }

    namespace {
        ui::HudItem ToHudItem(const BedrockSession::ItemStackInfo& info) {
            ui::HudItem item;
            if (info.Empty())
                return item;
            item.name = info.name;
            item.count = info.count;
            item.blockRuntimeId = info.blockRuntimeId;
            item.enchanted = info.enchanted;
            item.maxDamage = ui::MaxDurability(info.name);
            item.aux = item.maxDamage > 0 ? std::clamp(info.damage, 0, item.maxDamage) : info.aux;
            return item;
        }
    }

    namespace {
        bool IsInteractable(const std::string& type) {
            if (type == "minecraft:cushion")
                return true;
            if (type.find("boat") != std::string::npos || type.find("raft") != std::string::npos)
                return type.find("chest") == std::string::npos;
            if (type.find("minecart") != std::string::npos)
                return type == "minecraft:minecart";
            return type == "minecraft:horse" || type == "minecraft:donkey" || type == "minecraft:mule" || type == "minecraft:camel" ||
                   type == "minecraft:strider" || type == "minecraft:pig" || type == "minecraft:skeleton_horse" || type == "minecraft:zombie_horse";
        }
    }

    void Perlin::UpdateInventoryScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_INVENTORY") != nullptr;
        const auto now = Clock::now();
        if (live)
            live->InvPump();
        const bool canOpen = (live || preview) && !deathShown && !pauseShown && !chat.IsOpen() && state.mode != ui::GameMode::Spectator;
        if (!invShown && canOpen && (inputManager.Pressed(Action::Inventory) || (preview && !invPreviewOpened))) {
            invPreviewOpened = true;
            invShown = true;
            invOpenedAt = now;
            invPicked = -1;
            invSignature.clear();
            inputManager.TakeTyped();
            input.SetTextMode(true);
            if (live)
                live->OpenInventory();
        }
        if (!invShown)
            return;
        bool close = false;
        if (deathShown || (live && live->TakeServerClosedInventory()))
            close = true;
        for (const auto& t : inputManager.TakeTyped()) {
            if (now - invOpenedAt < std::chrono::milliseconds(150))
                continue;
            if (!t.character && t.key == Key::Escape)
                close = true;
            else if (t.character && (t.key == 'e' || t.key == 'E'))
                close = true;
            else if (t.character && (t.key == 'q' || t.key == 'Q') && live && invHoverAddr >= 0)
                live->InvDropSlot(invHoverAddr, t.ctrl);
            else if (t.character && t.key >= '1' && t.key <= '9' && live && invHoverAddr >= 0)
                live->InvHotbarKey(invHoverAddr, t.key - '1');
        }
        if (close) {
            if (live)
                live->CloseInventory();
            hud.Overlay().ClearOverlay();
            invShown = false;
            invHover = -1;
            invHoverAddr = -1;
            if (!chat.IsOpen() && !deathShown)
                input.SetTextMode(false);
            return;
        }

        ui::InventoryView v;
        if (live) {
            const auto inv = live->GetInventory();
            for (int i = 0; i < 9; i++)
                v.slots[i] = ToHudItem(inv.hotbar[i]);
            for (int i = 0; i < 27; i++)
                v.slots[9 + i] = ToHudItem(inv.main[i]);
            for (int i = 0; i < 4; i++) {
                v.armor[i] = ToHudItem(inv.armor[i]);
                v.craft[i] = ToHudItem(inv.craft[i]);
            }
            v.offhand = ToHudItem(inv.offhand);
            v.output = ToHudItem(inv.output);
            v.cursor = ToHudItem(inv.cursor);
            v.selectedHotbar = inv.selected;
        } else {
            for (int i = 0; i < 9; i++)
                v.slots[i] = state.hotbar[i];
            v.slots[9].name = "minecraft:cobblestone"; v.slots[9].count = 64;
            v.slots[10].name = "minecraft:oak_planks"; v.slots[10].count = 12;
            v.slots[20].name = "minecraft:diamond"; v.slots[20].count = 3;
            for (int i = 0; i < 4; i++)
                v.armor[i] = state.armorItems[i];
            v.selectedHotbar = state.selectedSlot;
        }

        if (now - invCheckAt > std::chrono::milliseconds(2000) || invTemplate.empty()) {
            invCheckAt = now;
            uint64_t version = 0;
            std::string text = ui::LoadUiTemplate(paths::DataDir(), "inventory_screen.rml", ui::DefaultInventoryScreenTemplate(), version);
            if (version != invTemplateVersion || invTemplate.empty()) {
                invTemplateVersion = version;
                invTemplate = std::move(text);
                invSignature.clear();
                std::cout << "[ui] inventory screen: loaded " << paths::DataDir() << "/ui/inventory_screen.rml" << std::endl;
            }
        }

        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
        invMouseX = gx;
        invMouseY = gy;
        if (hud.Overlay().HasOverlay()) {
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, options.guiScale);
            invHoverAddr = ui::InventoryAddrFromId(hud.Overlay().OverlayHover());
        }
        v.hovered = invHoverAddr;
        v.mouseX = gx;
        v.mouseY = gy;

        const bool followsMouse = !v.cursor.Empty() || invHoverAddr >= 0;
        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + std::to_string(invHoverAddr);
        if (followsMouse)
            signature += "|" + std::to_string(static_cast<int>(gx)) + "," + std::to_string(static_cast<int>(gy));
        const auto sig = [&](const ui::HudItem& it) { signature += it.name + ":" + std::to_string(it.count) + ":" + std::to_string(it.aux) + (it.enchanted ? "g" : "") + ";"; };
        for (const auto& it : v.slots) sig(it);
        for (const auto& it : v.armor) sig(it);
        for (const auto& it : v.craft) sig(it);
        sig(v.offhand);
        sig(v.output);
        sig(v.cursor);
        if (signature != invSignature || !hud.Overlay().HasOverlay()) {
            invSignature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildInventoryScreenRml(invTemplate, v, guiW, guiH), error)) {
                std::cout << "[ui] inventory_screen.rml: " << error << std::endl;
                invTemplate = ui::DefaultInventoryScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildInventoryScreenRml(invTemplate, v, guiW, guiH), error);
            }
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, options.guiScale);
        }

        const bool left = input.IsMouseDown(Mouse::Left), right = input.IsMouseDown(Mouse::Right);
        const bool fresh = now - invOpenedAt > std::chrono::milliseconds(150);
        if (live && fresh && ((left && !invLeftDown) || (right && !invRightDown))) {
            const int px = (guiW - ui::kInvPanelW) / 2, py = (guiH - ui::kInvPanelH) / 2;
            const bool outside = gx < px || gy < py || gx > px + ui::kInvPanelW || gy > py + ui::kInvPanelH;
            const bool isRight = !(left && !invLeftDown);
            if (invHoverAddr >= 0) {
                if (input.IsKeyDown(Key::Shift))
                    live->InvShiftClick(invHoverAddr);
                else
                    live->InvClick(invHoverAddr, isRight);
            } else if (outside) {
                live->InvDropCursor(!isRight);
            }
        }
        invLeftDown = left;
        invRightDown = right;
    }

    void Perlin::UpdatePauseScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_PAUSE") != nullptr;
        const auto now = Clock::now();
        const bool canOpen = (live || preview) && !deathShown && !invShown && !chat.IsOpen();
        if (!pauseShown && canOpen && (inputManager.Pressed(Action::Pause) || (preview && !pausePreviewOpened))) {
            pausePreviewOpened = true;
            pauseShown = true;
            pauseOpenedAt = now;
            pauseSignature.clear();
            inputManager.TakeTyped();
            input.SetTextMode(true);
        } else if (!pauseShown && !live && !preview && inputManager.Pressed(Action::Pause)) {
            platform->SetCursorCaptured(false);
        }
        if (!pauseShown)
            return;
        bool resume = deathShown || invShown || chat.IsOpen();
        for (const auto& t : inputManager.TakeTyped())
            if (!t.character && t.key == Key::Escape && now - pauseOpenedAt > std::chrono::milliseconds(100))
                resume = true;
        if (resume) {
            hud.Overlay().ClearOverlay();
            pauseShown = false;
            if (!chat.IsOpen() && !deathShown && !invShown)
                input.SetTextMode(false);
            return;
        }

        if (now - pauseCheckAt > std::chrono::milliseconds(400) || pauseTemplate.empty()) {
            pauseCheckAt = now;
            uint64_t version = 0;
            std::string text = ui::LoadUiTemplate(paths::DataDir(), "pause_screen.rml", ui::DefaultPauseScreenTemplate(), version);
            if (version != pauseTemplateVersion || pauseTemplate.empty()) {
                pauseTemplateVersion = version;
                pauseTemplate = std::move(text);
                pauseSignature.clear();
                std::cout << "[ui] pause screen: loaded " << paths::DataDir() << "/ui/pause_screen.rml" << std::endl;
            }
        }
        ui::PauseScreenText t;
        t.title = i18n::Tr("menu.game");
        t.back = i18n::Tr("menu.returnToGame");
        t.options = i18n::Tr("menu.options");
        t.quit = i18n::Tr("menu.disconnect");
        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        const std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + t.title + t.back + t.options + t.quit;
        if (signature != pauseSignature || !hud.Overlay().HasOverlay()) {
            pauseSignature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildPauseScreenRml(pauseTemplate, t, guiW, guiH), error)) {
                std::cout << "[ui] pause_screen.rml: " << error << std::endl;
                pauseTemplate = ui::DefaultPauseScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildPauseScreenRml(pauseTemplate, t, guiW, guiH), error);
            }
        }
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        const bool down = input.IsMouseDown(0);
        if (!down && pauseLeftDown && now - pauseOpenedAt > std::chrono::milliseconds(150)) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kPauseResumeId) {
                hud.Overlay().ClearOverlay();
                pauseShown = false;
                input.SetTextMode(false);
            } else if (hover == ui::kPauseQuitId && !preview) {
                platform->isRunning = false;
            }
        }
        pauseLeftDown = down;
    }

    bool Perlin::UpdateDisconnectScreen(ui::Hud& hud, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_DISCONNECT") != nullptr;
        const bool failed = preview || (session && session->GetState() == BedrockSession::State::Failed && !session->Transferring());
        const auto now = Clock::now();
        if (!failed) {
            if (disconnectShown) {
                hud.Overlay().ClearOverlay();
                disconnectShown = false;
                disconnectSignature.clear();
            }
            if (disconnectTextMode) {
                disconnectTextMode = false;
                if (!chat.IsOpen())
                    input.SetTextMode(false);
            }
            disconnectLeftDown = false;
            return false;
        }
        if (!disconnectShown) {
            disconnectShown = true;
            disconnectOpenedAt = now;
            pauseShown = invShown = deathShown = false;
            pauseSignature.clear();
            deathSignature.clear();
            std::cout << "[ui] disconnect screen shown" << std::endl;
        }
        if (!disconnectTextMode) {
            disconnectTextMode = true;
            input.SetTextMode(true);
        }

        ui::DisconnectScreenText t;
        t.rejoin = i18n::Tr("perlin.disconnect.rejoin");
        t.quit = i18n::Tr("menu.disconnect");
        BedrockSession::DisconnectInfo info;
        std::string lastError;
        if (preview) {
            info.happened = true;
            info.reason = 55;
            info.message = PERLIN_ENV("PERLIN_PREVIEW_DISCONNECT")[0] == 'b'
                               ? "You are banned from this server.\nReason: Cheating (fly hacks)\nExpires in 29d 23h 59m"
                               : "Kicked by an operator";
        } else {
            info = session->GetDisconnectInfo();
            lastError = session->LastError();
        }
        std::string message = ui::StripFormatting(info.message);
        if (!message.empty() && message.find(' ') == std::string::npos && message.find('.') != std::string::npos) {
            const std::string translated = i18n::Tr(message);
            if (translated != message)
                message = translated;
        }
        if (info.happened && info.connectionLost) {
            t.title = i18n::Tr("perlin.disconnect.lost");
            t.reason = i18n::Tr("perlin.disconnect.lost_reason");
            if (!message.empty())
                t.reason += "\n" + message;
        } else if (info.happened) {
            const bool ban = ui::LooksLikeBan(message);
            t.title = i18n::Tr(ban ? "perlin.disconnect.banned" : "perlin.disconnect.title");
            t.reason = message.empty() ? i18n::Tr("perlin.disconnect.no_reason") : message;
            switch (info.reason) {
                case 44: t.reason += "\n\n" + i18n::Tr("perlin.disconnect.hint.id_conflict"); break;
                case 8:
                case 35: t.reason += "\n\n" + i18n::Tr("perlin.disconnect.hint.version"); break;
                case 46: t.reason += "\n\n" + i18n::Tr("perlin.disconnect.hint.login"); break;
                default: break;
            }
            if (info.reason >= 0)
                t.code = i18n::Tr("perlin.disconnect.code", {std::to_string(info.reason)});
        } else {
            t.title = i18n::Tr("perlin.disconnect.failed");
            t.reason = lastError.empty() ? i18n::Tr("perlin.disconnect.no_reason") : lastError;
        }

        if (now - disconnectCheckAt > std::chrono::milliseconds(400) || disconnectTemplate.empty()) {
            disconnectCheckAt = now;
            uint64_t version = 0;
            std::string text = ui::LoadUiTemplate(paths::DataDir(), "disconnect_screen.rml", ui::DefaultDisconnectScreenTemplate(), version);
            if (version != disconnectTemplateVersion || disconnectTemplate.empty()) {
                disconnectTemplateVersion = version;
                disconnectTemplate = std::move(text);
                disconnectSignature.clear();
                std::cout << "[ui] disconnect screen: loaded " << paths::DataDir() << "/ui/disconnect_screen.rml" << std::endl;
            }
        }
        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        const std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + t.title + "|" + t.reason + "|" + t.code;
        if (signature != disconnectSignature || !hud.Overlay().HasOverlay()) {
            disconnectSignature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildDisconnectScreenRml(disconnectTemplate, t, guiW, guiH), error)) {
                std::cout << "[ui] disconnect_screen.rml: " << error << std::endl;
                disconnectTemplate = ui::DefaultDisconnectScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildDisconnectScreenRml(disconnectTemplate, t, guiW, guiH), error);
            }
        }
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && disconnectLeftDown && now - disconnectOpenedAt > std::chrono::milliseconds(300) && !preview) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kDisconnectRejoinId) {
                std::cout << "[ui] disconnect screen: rejoin" << std::endl;
                hud.Overlay().ClearOverlay();
                disconnectShown = false;
                disconnectSignature.clear();
                if (sessionReady && session && session->RequestReconnect()) {
                } else {
                    sessionReady = false;
                    Connect();
                }
            } else if (hover == ui::kDisconnectQuitId) {
                platform->isRunning = false;
            }
        }
        disconnectLeftDown = down;
        return true;
    }

    void Perlin::UpdateChatClicks(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        if (!chat.IsOpen() || (!live && !PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))) {
            chatLeftDown = false;
            return;
        }
        const int scale = hud.OverlayScale();
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        const bool down = input.IsMouseDown(0);
        if (down && !chatLeftDown) {
            const std::string link = hud.Overlay().HoverLink();
            if (link.size() > 6 && link[5] == '/' && live) {
                live->SendCommand(link.substr(6));
                chat.Close();
                input.SetTextMode(false);
            }
        }
        chatLeftDown = down;
    }

    void Perlin::FillHudState(ui::HudState& s, BedrockSession* live, bool preview) {
        s.visible = options.showHud && (live || preview);
        s.tick = preview ? static_cast<uint64_t>(std::chrono::duration<double>(Clock::now().time_since_epoch()).count() * 20.0)
                         : hudAnim.ticks;
        s.inWater = headUnderwater;
        chat.Fill(s, hudAnim.ticks, MsNow());
        if (preview) {
            s.health = 20.0f;
            s.food = 20;
            s.xpProgress = 0.55f;
            s.xpLevel = 7;
            s.armor = 0;
            s.selectedSlot = previewSlot;
            static const struct { const char* name; int count; int maxDamage; int aux; } kItems[9] = {
                {"minecraft:diamond_sword", 1, 1561, 120}, {"minecraft:grass_block", 64, 0, 0},
                {"minecraft:oak_planks", 12, 0, 0}, {"minecraft:torch", 33, 0, 0},
                {"minecraft:apple", 5, 0, 0}, {"minecraft:cooked_beef", 2, 0, 0},
                {"minecraft:iron_pickaxe", 1, 250, 40}, {"minecraft:oak_log", 20, 0, 0}, {"minecraft:bucket", 1, 0, 0}};
            for (int i = 0; i < 9; i++) {
                s.hotbar[i] = {};
                s.hotbar[i].name = kItems[i].name;
                s.hotbar[i].count = kItems[i].count;
                s.hotbar[i].maxDamage = kItems[i].maxDamage;
                s.hotbar[i].aux = kItems[i].aux;
                s.hotbar[i].enchanted = PERLIN_ENV("PERLIN_PREVIEW_ENCHANT") != nullptr;
            }
            if (const char* item = PERLIN_ENV("PERLIN_PREVIEW_ITEM")) {
                ui::HudItem& h = s.hotbar[std::clamp(previewSlot, 0, 8)];
                h.name = item;
                h.count = 1;
                h.maxDamage = h.aux = 0;
            }
            if (const char* tab = PERLIN_ENV("PERLIN_PREVIEW_TAB")) {
                s.tabOpen = true;
                s.tabEntries.clear();
                Texture head;
                head.width = head.height = 8;
                head.pixels.assign(8 * 8 * 4, 255);
                for (size_t i = 0; i < 64; i++) { head.pixels[i * 4] = 170; head.pixels[i * 4 + 1] = 120; head.pixels[i * 4 + 2] = 90; }
                ui::UiAssets::Instance().SetHead("preview", head);
                for (int i = 0; i < std::atoi(tab); i++)
                    s.tabEntries.push_back({"Player" + std::to_string(i + 1), "preview"});
            }
            if (PERLIN_ENV("PERLIN_PREVIEW_EMPTYHAND"))
                s.hotbar[previewSlot] = {};
            if (const char* n = PERLIN_ENV("PERLIN_PREVIEW_ITEMNAME")) {
                s.itemName = n;
                s.itemNameAlpha = 1.0f;
            }
            s.displayHealth = s.health;
            return;
        }
        if (!live)
            return;

        const auto vitals = live->GetVitals();
        const auto inv = live->GetInventory();
        switch (live->GameType()) {
        case 1: s.mode = ui::GameMode::Creative; break;
        case 2: s.mode = ui::GameMode::Adventure; break;
        case 3: case 4: case 6: s.mode = ui::GameMode::Spectator; break;
        default: s.mode = ui::GameMode::Survival; break;
        }
        s.health = vitals.health;
        s.maxHealth = std::max(1.0f, vitals.maxHealth);
        s.absorption = vitals.absorption;
        s.food = static_cast<int>(std::lround(vitals.hunger));
        s.saturation = vitals.saturation;
        s.xpLevel = vitals.level;
        s.xpProgress = vitals.xpProgress;
        s.air = vitals.air;
        s.maxAir = std::max(1, vitals.maxAir);
        s.poison = vitals.poison;
        s.wither = vitals.wither;
        s.regeneration = vitals.regeneration;
        s.hungerEffect = vitals.hungerEffect;

        s.selectedSlot = std::clamp(inv.selected, 0, 8);
        for (int i = 0; i < 9; i++)
            s.hotbar[i] = ToHudItem(inv.hotbar[i]);
        s.offhand = ToHudItem(inv.offhand);
        int armor = 0;
        for (int i = 0; i < 4; i++) {
            s.armorItems[i] = ToHudItem(inv.armor[i]);
            armor += ui::ArmorPoints(inv.armor[i].name);
        }
        s.armor = std::min(20, armor);

        s.healthBlinkUntil = hudAnim.blinkUntil;
        s.displayHealth = static_cast<float>(hudAnim.displayHealth);

        s.tabOpen = inputManager.IsDown(Action::PlayerList) && !inputManager.Blocked();
        s.tabEntries.clear();
        if (s.tabOpen) {
            auto players = live->PlayerList();
            std::stable_sort(players.begin(), players.end(), [](const BedrockSession::TabPlayer& a, const BedrockSession::TabPlayer& b) {
                std::string x = a.name, y = b.name;
                std::transform(x.begin(), x.end(), x.begin(), ::tolower);
                std::transform(y.begin(), y.end(), y.begin(), ::tolower);
                return x < y;
            });
            for (const BedrockSession::TabPlayer& p : players) {
                std::string key = "default";
                if (p.skin && p.skin->skinWidth >= 64 && p.skin->skinHeight >= 16) {
                    key = p.uuid + ":" + std::to_string(p.skin->serial);
                    if (!ui::UiAssets::Instance().HasHead(key)) {
                        const int sc = std::max(1, p.skin->skinWidth / 64);
                        Texture t;
                        t.width = t.height = 8;
                        t.pixels.assign(8 * 8 * 4, 255);
                        for (int y = 0; y < 8; y++)
                            for (int x = 0; x < 8; x++) {
                                const auto px = [&](int ox, int oy, uint8_t out[4]) {
                                    const size_t i = (static_cast<size_t>((oy + y) * sc + sc / 2) * p.skin->skinWidth +
                                                      static_cast<size_t>((ox + x) * sc + sc / 2)) * 4;
                                    for (int c = 0; c < 4; c++)
                                        out[c] = i + 3 < p.skin->skinRgba.size() ? p.skin->skinRgba[i + c] : 255;
                                };
                                uint8_t face[4], hat[4];
                                px(8, 8, face);
                                px(40, 8, hat);
                                uint8_t* o = &t.pixels[static_cast<size_t>(y * 8 + x) * 4];
                                const float a = hat[3] / 255.0f;
                                for (int c = 0; c < 3; c++)
                                    o[c] = static_cast<uint8_t>(face[c] * (1.0f - a) + hat[c] * a);
                                o[3] = 255;
                            }
                        ui::UiAssets::Instance().SetHead(key, std::move(t));
                    }
                } else if (!ui::UiAssets::Instance().HasHead(key)) {
                    Texture t;
                    t.width = t.height = 8;
                    t.pixels.assign(8 * 8 * 4, 255);
                    for (size_t i = 0; i < 64; i++) {
                        t.pixels[i * 4] = 170; t.pixels[i * 4 + 1] = 120; t.pixels[i * 4 + 2] = 90;
                    }
                    ui::UiAssets::Instance().SetHead(key, std::move(t));
                }
                s.tabEntries.push_back({p.name, key});
            }
        }

        const ui::HudItem& held = s.hotbar[s.selectedSlot];
        if (!held.Empty() && hudAnim.toolTimer > 0 && s.mode != ui::GameMode::Spectator) {
            s.itemName = ui::UiAssets::Instance().ItemName(held.name);
            if (advancedTooltips) {
                s.itemName += " (" + held.name + ")";
                if (held.maxDamage > 0)
                    s.itemName += " " + i18n::Tr("perlin.item.durability", {std::to_string(held.maxDamage - held.aux), std::to_string(held.maxDamage)});
            }
            s.itemNameAlpha = std::min(255.0f, static_cast<float>(hudAnim.toolTimer) * 256.0f / 10.0f) / 255.0f;
        } else {
            s.itemName.clear();
            s.itemNameAlpha = 0.0f;
        }

        const auto text = live->GetTitleText();
        if (hudAnim.overlayTime > 0 && !text.actionBar.empty()) {
            const float f = static_cast<float>(hudAnim.overlayTime) - hudPartial;
            s.actionBar = text.actionBar;
            s.actionBarAlpha = std::clamp(static_cast<int>(f * 255.0f / 20.0f), 0, 255) > 8
                                   ? std::min(255, static_cast<int>(f * 255.0f / 20.0f)) / 255.0f
                                   : 0.0f;
        } else {
            s.actionBar.clear();
            s.actionBarAlpha = 0.0f;
        }
        if (hudAnim.titleTime > 0 && !text.title.empty()) {
            const float f = static_cast<float>(hudAnim.titleTime) - hudPartial;
            int alpha = 255;
            if (hudAnim.titleTime > hudAnim.titleFadeOut + hudAnim.titleStay) {
                const float f1 = static_cast<float>(hudAnim.titleFadeIn + hudAnim.titleStay + hudAnim.titleFadeOut) - f;
                alpha = static_cast<int>(f1 * 255.0f / std::max(1, hudAnim.titleFadeIn));
            }
            if (hudAnim.titleTime <= hudAnim.titleFadeOut)
                alpha = static_cast<int>(f * 255.0f / std::max(1, hudAnim.titleFadeOut));
            alpha = std::clamp(alpha, 0, 255);
            s.title = text.title;
            s.subtitle = text.subtitle;
            s.titleAlpha = alpha > 8 ? static_cast<float>(alpha) / 255.0f : 0.0f;
        } else {
            s.title.clear();
            s.subtitle.clear();
            s.titleAlpha = 0.0f;
        }
    }

    void Perlin::TickHud() {
        HudAnim& a = hudAnim;
        a.ticks++;
        const int tick = static_cast<int>(a.ticks);
        ui::HudState& s = hudState;

        const int health = static_cast<int>(std::ceil(s.health));
        if (!a.haveHealth) {
            a.haveHealth = true;
            a.lastHealth = health;
            a.displayHealth = health;
            a.lastHealthTick = tick;
        }
        const bool invulnerable = tick - a.lastHurtTick < 20;
        if (health < a.lastHealth) {
            a.lastHurtTick = tick;
            a.lastHealthTick = tick;
            a.blinkUntil = tick + 20;
        } else if (health > a.lastHealth && invulnerable) {
            a.lastHealthTick = tick;
            a.blinkUntil = tick + 10;
        }
        if (tick - a.lastHealthTick > 20) {
            a.lastHealth = health;
            a.displayHealth = health;
            a.lastHealthTick = tick;
        }
        a.lastHealth = health;

        const ui::HudItem& held = s.hotbar[std::clamp(s.selectedSlot, 0, 8)];
        if (held.Empty()) {
            a.toolTimer = 0;
        } else if (!a.lastTool.Empty() && a.lastTool.name == held.name &&
                   (held.maxDamage > 0 || a.lastTool.aux == held.aux)) {
            if (a.toolTimer > 0)
                a.toolTimer--;
        } else {
            a.toolTimer = 40;
        }
        a.lastTool = held;

        if (a.overlayTime > 0)
            a.overlayTime--;
        if (a.titleTime > 0)
            a.titleTime--;
        if (session) {
            const auto text = session->GetTitleText();
            if (text.actionAt != a.actionAtSeen) {
                a.actionAtSeen = text.actionAt;
                if (!text.actionBar.empty())
                    a.overlayTime = 60;
            }
            if (text.titleAt != a.titleAtSeen) {
                a.titleAtSeen = text.titleAt;
                if (!text.title.empty()) {
                    a.titleFadeIn = std::max(0, text.fadeIn);
                    a.titleStay = std::max(0, text.stay);
                    a.titleFadeOut = std::max(0, text.fadeOut);
                    a.titleTime = a.titleFadeIn + a.titleStay + a.titleFadeOut;
                }
            }
            if (text.title.empty() && text.subtitle.empty())
                a.titleTime = 0;
        }
    }

    void Perlin::SpawnPreviewEntities() {
        const std::string list = PERLIN_ENV("PERLIN_PREVIEW_ENTITIES");
        std::vector<std::string> names;
        for (size_t start = 0; start <= list.size();) {
            const size_t comma = list.find(',', start);
            const std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!item.empty())
                names.push_back(item);
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        const float yawRad = glm::radians(camera.yaw);
        const glm::dvec3 forward(std::cos(yawRad), 0.0, std::sin(yawRad));
        const glm::dvec3 right(-forward.z, 0.0, forward.x);
        const double groundY = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_Y") ? std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_Y")) : camera.y - 1.62;
        const double distance = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_DIST") ? std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_DIST")) : 4.0;
        const double spacing = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_SPACING") ? std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_SPACING")) : 1.8;
        for (size_t i = 0; i < names.size(); i++) {
            std::string type = names[i], variant;
            if (const size_t colon = type.find(':'); colon != std::string::npos) {
                variant = type.substr(colon + 1);
                type.resize(colon);
            }
            const glm::dvec3 p = glm::dvec3(camera.x, groundY, camera.z) + forward * distance +
                                 right * ((static_cast<double>(i) - (names.size() - 1) * 0.5) * spacing);
            const float yaw = camera.yaw - 90.0f + 180.0f + (PERLIN_ENV("PERLIN_PREVIEW_ENTITY_YAW") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_YAW"))) : 0.0f);
            const uint64_t rid = 1000 + i;
            if (type == "item") {
                previewEntities.AddItem(rid, static_cast<int64_t>(rid), p, "minecraft:" + (variant.empty() ? std::string("diamond_sword") : variant),
                                        variant.find("stack") != std::string::npos ? 40 : 1, 0, 0, false);
            } else if (type == "player") {
                previewEntities.AddPlayer(rid, static_cast<int64_t>(rid), "preview" + std::to_string(i), "Steve",
                                          {p.x, p.y + entity::kPlayerEye, p.z}, 0.0f, yaw, yaw, 0, "");
                auto skin = std::make_shared<entity::SkinData>();
                skin->serial = 100 + i;
                const bool slim = variant.find("alex") != std::string::npos;
                Texture t;
                if (ui::UiAssets::Instance().Image(slim ? "textures/entity/alex" : "textures/entity/steve", t)) {
                    skin->skinWidth = t.width;
                    skin->skinHeight = t.height;
                    skin->skinRgba.assign(t.pixels.begin(), t.pixels.end());
                }
                skin->slim = slim;
                if (variant.find("cape") != std::string::npos) {
                    skin->capeWidth = 64;
                    skin->capeHeight = 32;
                    skin->capeRgba.assign(64 * 32 * 4, 255);
                    for (int y = 0; y < 32; y++)
                        for (int x = 0; x < 64; x++) {
                            uint8_t* px = &skin->capeRgba[(static_cast<size_t>(y) * 64 + x) * 4];
                            px[0] = static_cast<uint8_t>(150 + (x * 3) % 90);
                            px[1] = static_cast<uint8_t>(20 + y * 3);
                            px[2] = static_cast<uint8_t>(60 + ((x / 4 + y / 4) % 2) * 80);
                            px[3] = 255;
                        }
                }
                previewEntities.SetSkin("preview" + std::to_string(i), skin);
            } else {
                const uint64_t flags = variant.find("baby") != std::string::npos ? entity::flag::Baby : 0;
                previewEntities.AddEntity(rid, static_cast<int64_t>(rid), "minecraft:" + type, p, PERLIN_ENV("PERLIN_PREVIEW_ENTITY_PITCH") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_PITCH"))) : 0.0f, yaw, yaw, flags, 1.0f, 0, 0, 0);
            }
            if (type == "fishing_hook")
                previewEntities.SetOwner(rid, 1000);
            if (const char* held = PERLIN_ENV("PERLIN_PREVIEW_HELD"))
                previewEntities.SetHeld(rid, held);
            if (const char* armor = PERLIN_ENV("PERLIN_PREVIEW_ARMOR")) {
                static const char* parts[4] = {"helmet", "chestplate", "leggings", "boots"};
                for (int i = 0; i < 4; i++)
                    previewEntities.SetArmor(rid, i, std::string("minecraft:") + armor + "_" + parts[i]);
            }
            if (const char* nt = PERLIN_ENV("PERLIN_PREVIEW_NAMETAG"))
                previewEntities.SetNameTag(rid, nt);
            if (PERLIN_ENV("PERLIN_PREVIEW_SNEAK"))
                previewEntities.SetFlags(rid, entity::flag::Sneaking);
            if (PERLIN_ENV("PERLIN_PREVIEW_SWIM"))
                previewEntities.SetFlags(rid, entity::flag::Swimming | entity::flag::Sprinting);
            if (PERLIN_ENV("PERLIN_PREVIEW_DEAD"))
                previewEntities.Die(rid);
            if (PERLIN_ENV("PERLIN_PREVIEW_ENTITY_WALK"))
                previewWalk.push_back(rid);
        }
    }

    void Perlin::RenderThreadMain() {
        if (!renderer->Initialize(*platform, options)) {
            std::cout << "[render] renderer initialisation failed" << std::endl;
            platform->isRunning = false;
            return;
        }
        renderReady = true;
        StartSampleProfiler();
        levelRenderer = std::make_unique<LevelRenderer>(*renderer, options);
        handRenderer.Initialize(*renderer);
        entityRenderer.Initialize(*renderer);
        entityRenderer.SetItemRenderer(&handRenderer);

        camera.fov = options.fov;
        camera.nearPlane = 0.05f;
        camera.farPlane = std::max(static_cast<float>(options.renderDistance * 16 * 4),
                                   static_cast<float>(GraphicsSettings::From(options).cloudRange * 16));

        Zoom zoom(options.zoom);
        float scopeFov = 1.0f;
        auto previous = Clock::now();
        const auto startedAt = previous;
        uint64_t frameCounter = 0;
        DamageEffect damage;
        uint32_t lastHurtCount = 0, lastHurtSerial = 0;
        auto titleAt = previous;
        auto animationAt = previous;
        uint64_t animationStep = 0;

        const bool preview = PERLIN_ENV("PERLIN_PREVIEW") != nullptr;
        std::unique_ptr<Level> previewLevel;
        double previewTicks = kNoonTick;
        std::string previewShot;
        double previewWait = 15.0;
        int previewShotFrames = -1;
        auto previewReadyAt = previous;
        size_t previewLastMeshed = 0;
        if (preview) {
            camera.x = 24.0f; camera.y = 74.0f; camera.z = -22.0f; camera.yaw = 100.0f; camera.pitch = -6.0f;
            if (const char* c = PERLIN_ENV("PERLIN_PREVIEW_CAM"))
                std::sscanf(c, "%f,%f,%f,%f,%f", &camera.x, &camera.y, &camera.z, &camera.yaw, &camera.pitch);
            if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_PERSPECTIVE"))
                camera.mode = static_cast<PerspectiveMode>(std::atoi(m) % 3);
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_TIME"))
                previewTicks = std::atof(t);
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_SHOT"))
                previewShot = t;
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_WAIT"))
                previewWait = std::atof(t);
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_SWING"))
                handRenderer.ForceSwingProgress(static_cast<float>(std::atof(t)));
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_SLOT"))
                previewSlot = std::clamp(std::atoi(t), 0, 8);
            if (PERLIN_ENV("PERLIN_PREVIEW_DEBUG"))
                debugOn = true;
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_CHAT")) {
                std::string all = t;
                size_t from = 0;
                while (from <= all.size()) {
                    size_t bar = all.find('|', from);
                    if (bar == std::string::npos)
                        bar = all.size();
                    chat.AddMessage(all.substr(from, bar - from), UINT64_MAX / 2);
                    from = bar + 1;
                }
            }
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_CHATOPEN")) {
                std::vector<ui::CommandInfo> fake;
                for (const char* n : {"time", "tp", "teleport", "tell", "title", "tag", "tellraw", "gamemode", "give"}) {
                    ui::CommandInfo c;
                    c.name = n;
                    c.overloads.push_back({ui::CommandParam{"target", {}, true, false}, ui::CommandParam{"mode", {"survival", "creative", "adventure"}, false, true}});
                    fake.push_back(std::move(c));
                }
                chat.SetCommands(std::move(fake));
                chat.Open(t, MsNow());
                chatOpenedAt = Clock::now() - std::chrono::seconds(5);
                input.SetTextMode(true);
            }
        }
        int frames = 0;
        auto handLightAt = previous;
        int handSky = 15, handBlock = 0;

        while (renderRunning && platform->isRunning) {
            const auto frameStart = Clock::now();
            i18n::Lang::Get().Configure(ui::UiAssets::Instance().VanillaRoot(), paths::DataDir(), options.language);
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
            {
                const float target = handRenderer.Scoping() && camera.mode == PerspectiveMode::FirstPerson ? 0.1f : 1.0f;
                scopeFov += (target - scopeFov) * (1.0f - std::pow(0.5f, dt * 20.0f));
            }
            camera.fov = zoom.Fov(options.fov * (options.fovEffects ? player.FovModifier() : 1.0f)) * scopeFov;
            if (sessionReady) {
                const uint32_t hurts = session->HurtCount();
                const auto source = session->LatestHurtSource();
                const auto bearing = [&] {
                    if (!source.known)
                        return static_cast<float>(std::rand() % 2) * 180.0f;
                    const float javaYaw = camera.yaw - 90.0f;
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

            BedrockSession* live = sessionReady && !session->Transferring() ? session.get() : nullptr;
            if (live) {
                live->Update();
                live->PumpRequests(256);
                {
                    const int64_t rideUid = live->RidingUid();
                    std::vector<physics::Box> solid;
                    entity::Entity seatVehicle;
                    bool seated = false;
                    if (rideUid != 0 && live->Entities().FindUnique(rideUid, seatVehicle)) {
                        const glm::dvec3 seat = entityRenderer.SeatPosition(seatVehicle);
                        player.SetRidePosition(true, seat.x, seat.y, seat.z);
                        seated = true;
                    }
                    if (!seated)
                        player.SetRidePosition(false, 0, 0, 0);
                    for (const entity::Entity& e : live->Entities().Snapshot()) {
                        if (e.type != "minecraft:cushion" || (seated && e.uniqueId == rideUid))
                            continue;
                        float w = 0.999f, h = 0.249f;
                        entityRenderer.CollisionBoxOf(e.type, w, h);
                        solid.push_back({e.pos.x - w / 2, e.pos.y, e.pos.z - w / 2, e.pos.x + w / 2, e.pos.y + h, e.pos.z + w / 2});
                    }
                    SetEntityCollisionBoxes(std::move(solid));
                }
                player.SetItemSlow(gItemSlow);
                player.Update(*live, inputManager, camera, levelRenderer->Blocks(), dt);
            }
            if (const char* g = PERLIN_ENV("PERLIN_GLASS"))
                options.glass = g;
            levelRenderer->SetGlassMode(options.glass == "connected" ? 1 : options.glass == "connectedClear" ? 2 : options.glass == "clear" ? 3 : 0);
            levelRenderer->ReloadAssetsIfNeeded();

            if (live)
                for (const std::string& line : live->TakeChat())
                    chat.AddMessage(line, hudAnim.ticks);
            if (live) {
                if (const uint32_t v = live->CommandsVersion(); v != commandsSeen) {
                    commandsSeen = v;
                    chat.SetCommands(live->Commands());
                }
                if (chat.IsOpen() && Clock::now() - playersAt > std::chrono::seconds(1)) {
                    playersAt = Clock::now();
                    std::vector<std::string> names;
                    for (const auto& p : live->PlayerList())
                        names.push_back(p.name);
                    chat.SetPlayers(std::move(names));
                }
            }
            if (live || preview) {
                if (!chat.IsOpen()) {
                    if (inputManager.Pressed(Action::Chat) || inputManager.Pressed(Action::Command)) {
                        chat.Open(inputManager.Pressed(Action::Chat) ? "" : "/", MsNow());
                        chatOpenedAt = Clock::now();
                        input.SetTextMode(true);
                    }
                } else {
                    auto typed = inputManager.TakeTyped();
                    if (Clock::now() - chatOpenedAt < std::chrono::milliseconds(150))
                        typed.erase(std::remove_if(typed.begin(), typed.end(),
                                                   [](const InputManager::Typed& t) {
                                                       return t.character && (t.key == 't' || t.key == 'T' || t.key == '/');
                                                   }),
                                    typed.end());
                    const ui::ChatLog::Result r = chat.Type(typed, inputManager.Scroll());
                    if (r.send) {
                        if (live && r.text[0] == '/')
                            live->SendCommand(r.text.substr(1));
                        else if (live)
                            live->SendChat(r.text);
                        else
                            chat.AddMessage(i18n::Tr("chat.type.text", {i18n::Tr("perlin.chat.preview_player"), r.text}), hudAnim.ticks);
                    }
                    if (r.close) {
                        chat.Close();
                        input.SetTextMode(false);
                    }
                }
            } else if (chat.IsOpen()) {
                chat.Close();
                input.SetTextMode(false);
            }

            if (inputManager.Pressed(Action::ToggleHud))
                options.showHud = !options.showHud;
            if ((live || preview) && input.Captured()) {
                int newSlot = -1;
                for (int i = 0; i < 9; i++)
                    if (inputManager.Pressed(static_cast<Action>(static_cast<int>(Action::Hotbar1) + i)))
                        newSlot = i;
                static float scrollAccum = 0.0f;
                if (!inputManager.IsDown(Action::Zoom)) {
                    scrollAccum += inputManager.Scroll();
                    while (scrollAccum >= 1.0f || scrollAccum <= -1.0f) {
                        const int step = scrollAccum > 0.0f ? 1 : -1;
                        scrollAccum -= static_cast<float>(step);
                        const int from = newSlot >= 0 ? newSlot : hudState.selectedSlot;
                        newSlot = ((from - step) % 9 + 9) % 9;
                    }
                }
                if (newSlot >= 0 && newSlot != hudState.selectedSlot) {
                    hudState.selectedSlot = newSlot;
                    if (live)
                        live->SelectHotbarSlot(newSlot);
                    else
                        previewSlot = newSlot;
                }
                const bool spectator = hudState.mode == ui::GameMode::Spectator;
                const bool creative = hudState.mode == ui::GameMode::Creative;
                const ui::HudItem& held = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)];

                gAim = AimHit{};
                if ((live || previewLevel) && !spectator && levelRenderer) {
                    const std::vector<entity::Entity> aimEntities = live ? live->Entities().Snapshot() : previewEntities.Snapshot();
                    gAim = Raycast(live ? live->NetLevel() : *previewLevel, &levelRenderer->Blocks(), aimEntities,
                                   [&](const entity::Entity& e) {
                                       const auto hb = entityRenderer.HitBox(e);
                                       return glm::vec2(hb.first, hb.second);
                                   },
                                   {camera.x, camera.y, camera.z}, glm::dvec3(camera.Direction()), 5.7, creative ? 7.0 : 3.0);
                }
                if (PERLIN_ENV("PERLIN_AIM_DEBUG"))
                    std::cout << "[aim] kind " << static_cast<int>(gAim.kind) << " block " << gAim.bx << "," << gAim.by << "," << gAim.bz
                              << " face " << gAim.face << " dist " << gAim.distance << std::endl;
                const float eyeF[3] = {camera.x, camera.y, camera.z};
                auto clickOf = [&](float out[3]) {
                    out[0] = static_cast<float>(gAim.point.x - gAim.bx);
                    out[1] = static_cast<float>(gAim.point.y - gAim.by);
                    out[2] = static_cast<float>(gAim.point.z - gAim.bz);
                };
                if (live && live->TakeBlockNamesLearned()) {
                    MesherInvalidateCaches();
                    live->NetLevel().MarkAllDirty();
                }
                static double breakClock = 0, useClock = 0, swingClock = 0;
                static bool breaking = false, usingItem = false;
                static int bkx = 0, bky = 0, bkz = 0, bkFace = 1;
                static float breakProgress = 0.0f;
                static int breakDelayTicks = 0;
                static int hitTicks = 0;
                breakClock += dt;
                useClock += dt;
                swingClock += dt;

                auto stopBreaking = [&]() {
                    if (breaking && live) {
                        live->SendPlayerAuthInputBlockAction(1  , bkx, bky, bkz, bkFace);
                        live->ClearCrack(bkx, bky, bkz);
                    }
                    breaking = false;
                    breakProgress = 0.0f;
                    gLocalCrack.active = false;
                };

                if (spectator) {
                    stopBreaking();
                } else {
                    const bool attackHeld = inputManager.IsDown(Action::Attack);
                    if (inputManager.Pressed(Action::Attack)) {
                        handRenderer.Attack(options.hand, hudState.Survival());
                        swingClock = 0;
                        if (live) {
                            live->SendSwingArm(gAim.kind == AimHit::Kind::Block ? "mine" : "attack");
                            if (gAim.kind == AimHit::Kind::Entity) {
                                float click[3] = {static_cast<float>(gAim.point.x), static_cast<float>(gAim.point.y),
                                                  static_cast<float>(gAim.point.z)};
                                live->SendAttackEntity(gAim.entity, eyeF, click);
                            }
                        }
                    }
                    if (live && attackHeld && gAim.kind == AimHit::Kind::Block) {
                        const bool newTarget = !breaking || gAim.bx != bkx || gAim.by != bky || gAim.bz != bkz;
                        if (newTarget && breakDelayTicks > 0 && !breaking) {
                        } else if (newTarget) {
                            stopBreaking();
                            bkx = gAim.bx; bky = gAim.by; bkz = gAim.bz; bkFace = gAim.face;
                            breaking = true;
                            breakProgress = 0.0f;
                            breakClock = 0;
                            hitTicks = 0;
                            live->SendPlayerAuthInputBlockAction(0  , bkx, bky, bkz, bkFace);
                        }
                        while (breaking && breakClock >= 0.05) {
                            breakClock -= 0.05;
                            if (breakDelayTicks > 0) {
                                breakDelayTicks--;
                                continue;
                            }
                            handRenderer.Swing(options.hand);
                            bool loaded = false;
                            const uint16_t id = live->NetLevel().GetBlock(bkx, bky, bkz, loaded);
                            const std::string blockName = loaded ? BlockPalette::Instance().Name(id) : std::string();
                            float step = 0.0f;
                            if (creative) {
                                step = 1.0f;
                            } else if (!blockName.empty() && blockName != "minecraft:air") {
                                destroy::Conditions c;
                                c.heldItem = held.Empty() ? std::string() : held.name;
                                c.onGround = player.State().onGround;
                                c.inWater = hudState.inWater;
                                step = destroy::ProgressPerTick(destroy::Lookup(blockName), c);
                            }
                            if (!live->ServerAuthoritativeBlockBreaking() && !creative)
                                live->SendPlayerAuthInputBlockAction(18  , bkx, bky, bkz, bkFace);
                            else
                                live->SendPlayerAuthInputBlockAction(27  , bkx, bky, bkz, bkFace);
                            breakProgress += step;
                            if (!creative) {
                                bool loadedHere = false;
                                const uint16_t hitId = live->NetLevel().GetBlock(bkx, bky, bkz, loadedHere);
                                if (loadedHere && hitId != 0)
                                    particles.Crack(bkx, bky, bkz, bkFace, hitId);
                            }
                            if (!creative && ++hitTicks % 4 == 0 && !blockName.empty())
                                sounds.PlayBlockEvent(blockName, "hit", glm::dvec3(bkx + 0.5, bky + 0.5, bkz + 0.5));
                            if (breakProgress >= 1.0f) {
                                live->SendPlayerAuthInputBlockAction(26  , bkx, bky, bkz, bkFace);
                                if (!live->ServerAuthoritativeBlockBreaking() && !creative)
                                    live->SendPlayerAuthInputBlockAction(2  , bkx, bky, bkz, bkFace);
                                if (!blockName.empty())
                                    sounds.PlayBlockEvent(blockName, "break", glm::dvec3(bkx + 0.5, bky + 0.5, bkz + 0.5));
                                {
                                    bool loadedHere = false;
                                    const uint16_t oldId = live->NetLevel().GetBlock(bkx, bky, bkz, loadedHere);
                                    if (loadedHere && oldId != 0)
                                        particles.Destroy(bkx, bky, bkz, oldId);
                                    lastLocalBreak = {bkx, bky, bkz, std::chrono::steady_clock::now()};
                                }
                                live->NetLevel().SetBlock(bkx, bky, bkz, 0);
                                for (int up = 1; up <= 4; up++) {
                                    bool aboveLoaded = false;
                                    const uint16_t aboveId = live->NetLevel().GetBlock(bkx, bky + up, bkz, aboveLoaded);
                                    const std::string aboveName = aboveLoaded ? BlockPalette::Instance().Name(aboveId) : std::string();
                                    if (aboveName.empty() || !destroy::NeedsSupportBelow(aboveName))
                                        break;
                                    live->NetLevel().SetBlock(bkx, bky + up, bkz, 0);
                                    if (up == 1 && !destroy::BreaksWholeStack(aboveName))
                                        break;
                                }
                                breaking = false;
                                breakProgress = 0.0f;
                                gLocalCrack.active = false;
                                live->ClearCrack(bkx, bky, bkz);
                                handRenderer.Swing(options.hand);
                                breakDelayTicks = 5;
                            } else {
                                gLocalCrack = {true, bkx, bky, bkz, std::clamp(static_cast<int>(breakProgress * 10.0f), 0, 9)};
                            }
                        }
                        if (swingClock > 0.3) {
                            swingClock = 0;
                            live->SendSwingArm("mine");
                        }
                    } else if (breaking) {
                        stopBreaking();
                    } else {
                        gLocalCrack.active = false;
                        if (!attackHeld)
                            breakDelayTicks = 0;
                    }
                    if (breakDelayTicks > 0 && breakClock >= 0.05 && !breaking) {
                        breakClock -= 0.05;
                        breakDelayTicks--;
                    }

                    const bool useHeld = inputManager.IsDown(Action::Use);
                    const bool usePressed = inputManager.Pressed(Action::Use);
                    static auto lastUseSent = Clock::time_point{};
                    const bool tooSoon = Clock::now() - lastUseSent < std::chrono::milliseconds(60);
                    if ((usePressed || (useHeld && useClock >= 0.2)) && !tooSoon) {
                        lastUseSent = Clock::now();
                        useClock = 0;
                        const bool holdsItem = !held.Empty();
                        const bool holdsBlock = holdsItem && held.blockRuntimeId != 0;
                        blockuse::Use use = blockuse::Use::Nothing;
                        int dest[3] = {0, 0, 0};
                        if (gAim.kind == AimHit::Kind::Block && (live || previewLevel)) {
                            Level& lv = live ? live->NetLevel() : *previewLevel;
                            const auto nameAt = [&](int x, int y, int z) {
                                bool loaded = false;
                                const uint16_t id = lv.GetBlock(x, y, z, loaded);
                                return loaded ? BlockPalette::Instance().Name(id) : std::string();
                            };
                            int clicked[3] = {gAim.bx, gAim.by, gAim.bz};
                            static const int kOff[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
                            const int f = std::clamp(gAim.face, 0, 5);
                            const blockuse::Result r = blockuse::Classify(
                                nameAt(clicked[0], clicked[1], clicked[2]),
                                nameAt(clicked[0] + kOff[f][0], clicked[1] + kOff[f][1], clicked[2] + kOff[f][2]), clicked, gAim.face,
                                holdsItem, holdsBlock, inputManager.IsDown(Action::Sneak));
                            use = r.use;
                            std::copy(r.dest, r.dest + 3, dest);
                        }
                        bool interactedEntity = false;
                        if (usePressed && live && gAim.kind == AimHit::Kind::Entity) {
                            entity::Entity target;
                            if (live->Entities().Find(gAim.entity, target) && IsInteractable(target.type)) {
                                float click[3] = {static_cast<float>(gAim.point.x - target.pos.x), static_cast<float>(gAim.point.y - target.pos.y),
                                                  static_cast<float>(gAim.point.z - target.pos.z)};
                                handRenderer.Swing(options.hand);
                                live->SendSwingArm("interact");
                                live->SendInteractEntity(gAim.entity, eyeF, click);
                                interactedEntity = true;
                            }
                        }
                        const bool allowed = usePressed || (use == blockuse::Use::Place);
                        if (gAim.kind == AimHit::Kind::Block && allowed) {
                            if (use != blockuse::Use::Nothing && usePressed)
                                handRenderer.Swing(options.hand);
                            if (live) {
                                float click[3];
                                clickOf(click);
                                if (use != blockuse::Use::Nothing && usePressed)
                                    live->SendSwingArm(use == blockuse::Use::Place ? "build" : "interact");
                                live->SendUseItemOnBlock(gAim.bx, gAim.by, gAim.bz, gAim.face, click, eyeF, static_cast<int>(use), dest);
                            }
                            if (use == blockuse::Use::Place && holdsBlock)
                                sounds.PlayBlockEvent(held.name, "place", glm::dvec3(dest[0] + 0.5, dest[1] + 0.5, dest[2] + 0.5));
                        }
                        if (usePressed && live && holdsItem && use == blockuse::Use::Nothing && !interactedEntity) {
                            live->SendUseItemInAir(eyeF);
                            usingItem = true;
                        }
                    }
                    if (!useHeld && live)
                        live->EndItemUseOn();
                    if (usingItem && !useHeld) {
                        usingItem = false;
                        if (live)
                            live->SendReleaseItem(eyeF);
                    }

                    if (live && inputManager.Pressed(Action::PickBlock) && gAim.kind == AimHit::Kind::Block)
                        live->SendPickBlock(gAim.bx, gAim.by, gAim.bz);

                    if (live && inputManager.Pressed(Action::Drop) && !held.Empty())
                        live->SendDropSelected(inputManager.IsDown(Action::Sprint) ? 0 : 1);
                }
            } else {
                gAim = AimHit{};
            }

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

            if (preview && previewLevel && PERLIN_ENV("PERLIN_PREVIEW_EDIT")) {
                static bool edited = false;
                if (!edited && frameStart - previewReadyAt > std::chrono::seconds(4)) {
                    edited = true;
                    const std::string list = PERLIN_ENV("PERLIN_PREVIEW_EDIT");
                    size_t start = 0;
                    while (start < list.size()) {
                        size_t end = list.find(';', start);
                        if (end == std::string::npos)
                            end = list.size();
                        int dx = 0, dy = 0, dz = 0;
                        char name[128] = {0};
                        if (std::sscanf(list.substr(start, end - start).c_str(), "%d,%d,%d,%127s", &dx, &dy, &dz, name) == 4) {
                            BlockPalette& palette = BlockPalette::Instance();
                            std::string full = name;
                            std::vector<std::pair<std::string, BlockPalette::StateValue>> states;
                            if (const size_t lb = full.find('['); lb != std::string::npos) {
                                std::string body = full.substr(lb + 1, full.size() - lb - 2);
                                full = full.substr(0, lb);
                                size_t q = 0;
                                while (q < body.size()) {
                                    size_t c = body.find(',', q);
                                    if (c == std::string::npos) c = body.size();
                                    const std::string kv = body.substr(q, c - q);
                                    const size_t eq = kv.find('=');
                                    if (eq != std::string::npos) {
                                        BlockPalette::StateValue sv;
                                        const std::string val = kv.substr(eq + 1);
                                        if (val == "true" || val == "false") { sv.kind = BlockPalette::StateValue::Bool; sv.i = val == "true"; }
                                        else if (!val.empty() && (std::isdigit(static_cast<unsigned char>(val[0])) || val[0] == '-')) { sv.kind = BlockPalette::StateValue::Int; sv.i = std::atoi(val.c_str()); }
                                        else { sv.kind = BlockPalette::StateValue::String; sv.s = val; }
                                        states.emplace_back(kv.substr(0, eq), sv);
                                    }
                                    q = c + 1;
                                }
                                std::sort(states.begin(), states.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                            }
                            const uint16_t id = palette.Intern(BlockPalette::HashState(full, states));
                            previewLevel->SetBlock(static_cast<int>(std::floor(camera.x)) + dx, static_cast<int>(std::floor(camera.y)) + dy,
                                                   static_cast<int>(std::floor(camera.z)) + dz, id);
                        }
                        start = end + 1;
                    }
                    std::cout << "[preview] edits applied" << std::endl;
                }
            }

            camera.thirdPersonDistance = 5.0f;
            if (live && camera.mode != PerspectiveMode::FirstPerson) {
                const LevelCollision col(live->NetLevel(), &levelRenderer->Blocks());
                const glm::vec3 d = camera.mode == PerspectiveMode::ThirdPersonBack ? -camera.Direction() : camera.Direction();
                for (float t = 0.3f; t <= 5.0f; t += 0.15f) {
                    const glm::vec3 q = glm::vec3(camera.x, camera.y, camera.z) + d * t;
                    const physics::BlockShape cell = col.Query(static_cast<int>(std::floor(q.x)), static_cast<int>(std::floor(q.y)),
                                                               static_cast<int>(std::floor(q.z)));
                    if (!cell.Empty()) {
                        camera.thirdPersonDistance = std::max(0.3f, t - 0.3f);
                        break;
                    }
                }
            }
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

            headUnderwater = underwater;
            if ((live || previewLevel) && frameStart - handLightAt > std::chrono::milliseconds(200)) {
                handLightAt = frameStart;
                const glm::vec3 e = camera.EyePosition();
                const int bx = static_cast<int>(std::floor(e.x)), by = static_cast<int>(std::floor(e.y)),
                          bz = static_cast<int>(std::floor(e.z));
                Level& lightLevel = live ? live->NetLevel() : *previewLevel;
                const uint8_t packed = CachedLight(lightLevel, bx, by, bz, frameStart);
                handSky = ChunkLighting::Result::Sky(packed);
                handBlock = ChunkLighting::Result::Block(packed);
                dbgSky = handSky;
                dbgBlock = handBlock;
            }
            FillHudState(hudState, live, preview);
            {
                static bool f3Was = false, combo = false;
                static bool keyWas[256] = {};
                static std::chrono::steady_clock::time_point cHeldSince{};
                static bool cHeld = false;
                const bool f3 = input.IsKeyDown(Key::F3) && !input.TextMode();
                const auto edge = [&](int key) {
                    const bool down = f3 && input.IsKeyDown(key);
                    const bool fired = down && !keyWas[key & 255];
                    keyWas[key & 255] = down;
                    if (fired)
                        combo = true;
                    return fired;
                };
                const auto say = [&](const std::string& text) {
                    chat.AddMessage(i18n::Tr("perlin.debug.prefix") + " " + text, hudAnim.ticks);
                };
                const auto onOff = [&](bool on) { return i18n::Tr(on ? "options.on" : "options.off"); };
                const auto num = [](double v) {
                    char b[32];
                    std::snprintf(b, sizeof b, "%.2f", v);
                    return std::string(b);
                };
                const float feetY = camera.y - 1.62f;
                const std::string dimName = live ? (live->DimensionId() == 1 ? "minecraft:the_nether" : live->DimensionId() == 2 ? "minecraft:the_end" : "minecraft:overworld")
                                                 : "minecraft:overworld";
                (void)dimName;
                if (edge('A')) {
                    if (live)
                        live->ReloadChunks();
                    else if (levelRenderer)
                        levelRenderer->RemeshAll();
                    say(i18n::Tr("perlin.debug.reload_chunks"));
                }
                if (edge('B')) {
                    showHitboxes = !showHitboxes;
                    say(i18n::Tr(showHitboxes ? "perlin.debug.hitboxes_on" : "perlin.debug.hitboxes_off"));
                }
                {
                    const bool cDown = f3 && input.IsKeyDown('C');
                    if (edge('C')) {
                        const std::string tp = "/tp @s " + num(camera.x) + " " + num(feetY) + " " + num(camera.z) + " " +
                                               num(camera.yaw - 90.0f) + " " + num(-camera.pitch);
                        platform->SetClipboard(tp);
                        say(i18n::Tr("perlin.debug.copied_location", {tp}));
                        cHeldSince = std::chrono::steady_clock::now();
                        cHeld = true;
                    }
                    if (!cDown)
                        cHeld = false;
                    else if (cHeld && std::chrono::steady_clock::now() - cHeldSince > std::chrono::seconds(10))
                        throw std::runtime_error(i18n::Tr("perlin.debug.manual_crash"));
                }
                if (edge('D')) {
                    chat.Clear();
                    say(i18n::Tr("perlin.debug.chat_cleared"));
                }
                if (edge('G')) {
                    showChunkBorders = !showChunkBorders;
                    say(i18n::Tr(showChunkBorders ? "perlin.debug.chunk_borders_on" : "perlin.debug.chunk_borders_off"));
                }
                if (edge('H')) {
                    advancedTooltips = !advancedTooltips;
                    say(i18n::Tr(advancedTooltips ? "perlin.debug.advanced_tooltips_on" : "perlin.debug.advanced_tooltips_off"));
                }
                if (edge('I')) {
                    if (gAim.kind == AimHit::Kind::Block && (live || previewLevel)) {
                        bool loaded = false;
                        Level& l = live ? live->NetLevel() : *previewLevel;
                        const std::string name = BlockPalette::Instance().Name(l.GetBlock(gAim.bx, gAim.by, gAim.bz, loaded));
                        const std::string cmd = "/setblock " + std::to_string(gAim.bx) + " " + std::to_string(gAim.by) + " " +
                                                std::to_string(gAim.bz) + " " + name;
                        platform->SetClipboard(cmd);
                        say(i18n::Tr("perlin.debug.copied_block", {cmd}));
                    } else if (gAim.kind == AimHit::Kind::Entity && live) {
                        entity::Entity target;
                        if (live->Entities().Find(gAim.entity, target)) {
                            const std::string cmd = "/summon " + target.type + " " + num(target.pos.x) + " " + num(target.pos.y) + " " + num(target.pos.z);
                            platform->SetClipboard(cmd);
                            say(i18n::Tr("perlin.debug.copied_entity", {cmd}));
                        }
                    } else {
                        say(i18n::Tr("perlin.debug.nothing_targeted"));
                    }
                }
                if (edge('N')) {
                    static int previousMode = static_cast<int>(ui::GameMode::Creative);
                    const ui::GameMode current = hudState.mode;
                    const auto modeName = [](int m) { return m == 0 ? "survival" : m == 1 ? "creative" : m == 2 ? "adventure" : "spectator"; };
                    if (live) {
                        if (current != ui::GameMode::Spectator) {
                            previousMode = static_cast<int>(current);
                            live->SendCommand("gamemode spectator");
                        } else {
                            live->SendCommand(std::string("gamemode ") + modeName(previousMode));
                        }
                    }
                    say(i18n::Tr("perlin.debug.gamemode_toggle"));
                }
                if (edge('P')) {
                    input.pauseOnLostFocus = !input.pauseOnLostFocus;
                    say(i18n::Tr(input.pauseOnLostFocus ? "perlin.debug.pause_on_lost_focus_on" : "perlin.debug.pause_on_lost_focus_off"));
                }
                if (edge('T')) {
                    if (levelRenderer)
                        levelRenderer->ReloadResources();
                    entityRenderer.ClearCaches();
                    ui::UiAssets::Instance().ClearItemCache();
                    i18n::Lang::Get().Invalidate();
                    sounds.Reload();
                    if (hud)
                        hud->Reload();
                    say(i18n::Tr("perlin.debug.reload_resources"));
                }
                if (edge(Key::F4)) {
                    if (switcherIndex < 0) {
                        const int cur = static_cast<int>(hudState.mode);
                        switcherIndex = cur == 1 ? 0 : cur == 0 ? 1 : cur == 2 ? 2 : 3;
                    }
                    switcherIndex = (switcherIndex + 1) % 4;
                }
                if (edge(Key::Escape)) {
                    softPaused = !softPaused;
                    inputManager.SetBlocked(softPaused);
                    say(i18n::Tr(softPaused ? "perlin.debug.paused" : "perlin.debug.unpaused"));
                }
                if (f3Was && !f3) {
                    if (switcherIndex >= 0) {
                        static const char* kModes[4] = {"creative", "survival", "adventure", "spectator"};
                        if (live)
                            live->SendCommand(std::string("gamemode ") + kModes[switcherIndex]);
                        switcherIndex = -1;
                    } else if (!combo) {
                        debugOn = !debugOn;
                    }
                    combo = false;
                }
                f3Was = f3;
                if (!f3)
                    std::fill(std::begin(keyWas), std::end(keyWas), false);
                if (switcherIndex >= 0) {
                    static const char* kKeys[4] = {"selectWorld.gameMode.creative", "selectWorld.gameMode.survival", "selectWorld.gameMode.adventure",
                                                   "selectWorld.gameMode.spectator"};
                    std::string line;
                    for (int i = 0; i < 4; i++) {
                        const std::string name = i18n::Tr(kKeys[i]);
                        line += (i == switcherIndex ? "\u00a7" "e[ " + name + " ]\u00a7" "r" : "\u00a7" "7" + name + "\u00a7" "r");
                        if (i < 3)
                            line += "   ";
                    }
                    hudState.actionBar = i18n::Tr("perlin.gamemode.switcher") + ": " + line;
                    hudState.actionBarAlpha = 1.0f;
                }
            }
            FillDebug(hudState, live, live ? &live->NetLevel() : previewLevel.get());
            UpdateAudio(live, std::chrono::duration<double>(frameStart - startedAt).count(), dt);

            tickDebt += dt * 20.0f;
            while (tickDebt >= 1.0) {
                tickDebt -= 1.0;
                blockLight.Tick();
                damage.Tick();
                if (live || preview) {
                    TickHud();
                    HandRenderer::TickIn handIn;
                    handIn.yaw = camera.yaw - 90.0f;
                    handIn.pitch = -camera.pitch;
                    handIn.main = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)];
                    handIn.off = hudState.offhand;
                    handIn.survival = hudState.Survival();
                    handIn.sneaking = inputManager.IsDown(Action::Sneak);
                    handIn.useHeld = (input.Captured() && inputManager.IsDown(Action::Use)) || PERLIN_ENV("PERLIN_PREVIEW_USE") != nullptr;
                    {
                        const std::string& mn = handIn.main.name;
                        const bool drink = mn == "minecraft:potion" || mn == "minecraft:milk_bucket" || mn == "minecraft:honey_bottle" ||
                                           mn == "minecraft:ominous_bottle";
                        const bool always = mn == "minecraft:golden_apple" || mn == "minecraft:enchanted_golden_apple" ||
                                            mn == "minecraft:chorus_fruit" || drink || mn == "minecraft:suspicious_stew";
                        handIn.eating = handIn.survival && handIn.useHeld && HandRenderer::IsFood(mn) &&
                                        (always || hudState.food < 20);
                    }
                    {
                        const std::string& hn = handIn.main.name;
                        gItemSlow = handIn.eating || (handIn.useHeld && (hn == "minecraft:bow" || hn == "minecraft:trident" ||
                                                                          hn == "minecraft:spyglass"));
                    }
                    handRenderer.Tick(handIn, options.hand);
                    {
                        const int et = handRenderer.EatTicks();
                        if (et >= 7 && et % 4 == 3) {
                            const std::string& mn = handIn.main.name;
                            const bool drink = mn == "minecraft:potion" || mn == "minecraft:milk_bucket" ||
                                               mn == "minecraft:honey_bottle" || mn == "minecraft:ominous_bottle";
                            sounds.PlayEntityEvent("minecraft:player", drink ? "drink" : "eat",
                                                   glm::dvec3(camera.x, camera.y, camera.z));
                        }
                    }
                }
                waterVisionTime = underwater ? std::min(600, waterVisionTime + 1) : std::max(0, waterVisionTime - 10);
                if (live) {
                    previousRainLevel = rainLevel;
                    previousThunderLevel = thunderLevel;
                    rainLevel = StepWeather(rainLevel, live->RainTarget());
                    thunderLevel = StepWeather(thunderLevel, live->ThunderTarget());
                }
            }
            const float partial = static_cast<float>(tickDebt);
            hudPartial = partial;
            const float rain = glm::mix(previousRainLevel, rainLevel, partial);
            const float thunder = glm::mix(previousThunderLevel, thunderLevel, partial) * rain;

            EnvironmentInput input;
            input.ticks = worldTicks;
            input.rain = rain;
            input.thunder = thunder;
            if (live)
                input.dimension = static_cast<Dimension>(std::clamp(live->DimensionId(), 0, 2));
            const Environment env = SampleEnvironment(input);

            glm::mat4 bobMatrix(1.0f);
            if (live && options.hand.viewBobbing && camera.mode == PerspectiveMode::FirstPerson)
                bobMatrix = BobViewMatrix(player.Bob(), player.PartialTick(), options.hand.bobStrength);
            const glm::mat4 hurtMatrix = sessionReady ? damage.ViewMatrix(partial, options.damageTilt) : glm::mat4(1.0f);
            camera.viewEffect = hurtMatrix * (options.hand.bobCamera ? bobMatrix : glm::mat4(1.0f));
            const glm::mat4 handViewEffect = hurtMatrix * (options.hand.bobHand ? bobMatrix : glm::mat4(1.0f));
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
            std::vector<dynlight::Light> dyn;
            int dynMode = options.dynamicLights == "fancy" ? 2 : options.dynamicLights == "fast" ? 1 : 0;
            if (const char* dl = PERLIN_ENV("PERLIN_DYNLIGHTS"))
                dynMode = std::string(dl) == "fancy" ? 2 : std::string(dl) == "fast" ? 1 : 0;
            if (dynMode && (live || previewLevel)) {
                const std::vector<entity::Entity> lightEntities = live ? live->Entities().Snapshot() : previewEntities.Snapshot();
                for (const entity::Entity& le : lightEntities)
                    if (!le.isSelf)
                        dynlight::Collect(le, dyn);
                if (hudState.mode != ui::GameMode::Spectator) {
                    const int own = std::max(dynlight::ItemLevel(hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)].name),
                                             dynlight::ItemLevel(hudState.offhand.name));
                    if (own > 0)
                        dyn.push_back({glm::dvec3(camera.x, camera.y - 0.25, camera.z), static_cast<float>(own)});
                }
                dynlight::Select(dyn, glm::dvec3(camera.x, camera.y, camera.z), 8);
            }
            FrameParams params{};
            params.motionBlur = options.motionBlur;
            if (const char* mb = PERLIN_ENV("PERLIN_MOTION_BLUR"))
                params.motionBlur = std::clamp(static_cast<float>(std::atof(mb)), 0.0f, 1.0f);
            params.dynMode = dynMode;
            params.dynCount = static_cast<int>(dyn.size());
            for (size_t i = 0; i < dyn.size(); i++) {
                params.dynLights[i][0] = static_cast<float>(dyn[i].pos.x - camera.x);
                params.dynLights[i][1] = static_cast<float>(dyn[i].pos.y - camera.y);
                params.dynLights[i][2] = static_cast<float>(dyn[i].pos.z - camera.z);
                params.dynLights[i][3] = dyn[i].level;
            }
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
            {
                const glm::mat4 handProjection = HandRenderer::Projection(70.0f, camera.aspect, camera.farPlane);
                std::copy_n(glm::value_ptr(handProjection), 16, params.handProjection);
            }

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

            params.crosshair = false;
            {
                int cw = 0, ch = 0;
                renderer->FramebufferSize(cw, ch);
                params.guiScale = ui::JavaGuiScale(cw, ch, options.guiScale);
            }
            params.screenTint[0] = 1.0f;
            params.screenTint[1] = 0.0f;
            params.screenTint[2] = 0.0f;
            params.screenTint[3] = options.damageTint ? damage.TintAlpha() : 0.0f;

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
                if (const char* h = PERLIN_ENV("PERLIN_HELDLIGHT"))
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

            if (gProfile.enabled)
                gProfile.ms[0] += std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count();
            renderer->BeginFrame(params);
            if (live) {
                { ProfScope p(1); levelRenderer->Update(&live->NetLevel(), camera.x, camera.z, frustum); }
                { ProfScope p(2); levelRenderer->Render(frustum, camera.x, camera.z); }
            } else if (previewLevel) {
                { ProfScope p(1); levelRenderer->Update(previewLevel.get(), camera.x, camera.z, frustum); }
                { ProfScope p(2); levelRenderer->Render(frustum, camera.x, camera.z); }
                if (!previewShot.empty() && previewShotFrames < 0) {
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
            if (live) {
                static const char* shot = PERLIN_ENV("PERLIN_LIVE_SHOT");
                static const char* swingAt = PERLIN_ENV("PERLIN_LIVE_SWING");
                static bool swung = false, shotDone = false;
                static int shotFrames = 0;
                const double t = std::chrono::duration<double>(frameStart - startedAt).count();
                if (swingAt && !swung && t > std::atof(swingAt)) {
                    swung = true;
                    handRenderer.Attack(options.hand, true);
                    live->SendSwingArm("attack");
                }
                if (shot) {
                    const std::string spec = shot;
                    const size_t at = spec.rfind('@');
                    const double when = at == std::string::npos ? 20.0 : std::atof(spec.c_str() + at + 1);
                    if (!shotDone && t > when) {
                        shotDone = true;
                        renderer->RequestScreenshot(spec.substr(0, at));
                    } else if (shotDone && ++shotFrames > 8) {
                        platform->isRunning = false;
                    }
                }
            }
            std::optional<entity::Entity> fpSelf;
            if ((live || previewLevel) && ui::UiAssets::Instance().VanillaRoot().size()) {
                Level& entityLevel = live ? live->NetLevel() : *previewLevel;
                if (!live && !previewEntitiesSpawned && PERLIN_ENV("PERLIN_PREVIEW_ENTITIES")) {
                    previewEntitiesSpawned = true;
                    SpawnPreviewEntities();
                }
                std::vector<entity::Entity> entities = live ? live->Entities().Snapshot() : previewEntities.Snapshot();
                if (!live && PERLIN_ENV("PERLIN_PREVIEW_RIDE") && entities.size() >= 2) {
                    std::sort(entities.begin(), entities.end(), [](const entity::Entity& a, const entity::Entity& b) { return a.runtimeId < b.runtimeId; });
                    entities[1].vehicleUid = entities[0].uniqueId;
                }
                entityRenderer.ResolveSeats(entities);
                if (!live && !previewWalk.empty()) {
                    static std::unordered_map<uint64_t, glm::dvec3> base;
                    static const auto t0 = std::chrono::steady_clock::now();
                    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                    for (entity::Entity& pe : entities)
                        if (std::find(previewWalk.begin(), previewWalk.end(), pe.runtimeId) != previewWalk.end()) {
                            const glm::dvec3 b = base.emplace(pe.runtimeId, pe.pos).first->second;
                            const double yawRad = glm::radians(static_cast<double>(pe.yaw));
                            pe.pos = b + glm::dvec3(-std::sin(yawRad), 0.0, std::cos(yawRad)) * std::sin(t * 1.5) * 1.5;
                            previewEntities.Move(pe.runtimeId, pe.pos, pe.pitch, pe.yaw, pe.headYaw, true);
                        }
                }
                const bool firstPerson = camera.mode == PerspectiveMode::FirstPerson;
                if (live || !firstPerson || PERLIN_ENV("PERLIN_PREVIEW_SELF")) {
                    entity::Entity self;
                    self.runtimeId = 0xFFFFFFFFFFFF0001ull;
                    self.type = "minecraft:player";
                    self.isPlayer = self.isSelf = true;
                    self.name = live ? live->PlayerName() : std::string("Perlin");
                    self.pos = {camera.x, camera.y - entity::kPlayerEye, camera.z};
                    self.yaw = self.headYaw = camera.yaw - 90.0f;
                    {
                        static float bodyYaw = 0.0f;
                        static glm::dvec3 lastBodyPos{};
                        static bool bodyInit = false;
                        const auto wrap = [](float a) { a = std::fmod(a + 180.0f, 360.0f); if (a < 0) a += 360.0f; return a - 180.0f; };
                        const float head = self.headYaw;
                        if (!bodyInit || PERLIN_ENV("PERLIN_PREVIEW_ENTITIES")) {
                            bodyInit = true;
                            bodyYaw = head;
                            lastBodyPos = self.pos;
                        }
                        const double moved = std::hypot(self.pos.x - lastBodyPos.x, self.pos.z - lastBodyPos.z);
                        lastBodyPos = self.pos;
                        const float steps = std::max(1.0f, dt * 20.0f);
                        if (moved / std::max(1e-4f, dt) > 0.4)
                            bodyYaw += wrap(head - bodyYaw) * (1.0f - std::pow(1.0f - 0.3f, steps));
                        float offset = wrap(head - bodyYaw);
                        offset = std::clamp(offset, -75.0f, 75.0f);
                        bodyYaw = head - offset;
                        if (offset * offset > 2500.0f)
                            bodyYaw += offset * 0.2f;
                        self.yaw = bodyYaw;
                    }
                    self.pitch = -camera.pitch;
                    self.moveSerial = 1;
                    self.mainHand = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)].name;
                    for (int i = 0; i < 4; i++)
                        self.armor[i] = hudState.armorItems[i].name;
                    self.offHand = hudState.offhand.name;
                    if (inputManager.IsDown(Action::Sneak))
                        self.flags |= entity::flag::Sneaking;
                    player.SetSwimming(live && player.Sprinting() && headUnderwater);
                    if (player.Swimming())
                        self.flags |= entity::flag::Swimming;
                    if (live) {
                        const LoadedSkin& ls = live->Skin();
                        const std::string key = ls.skinId + (ls.slimArms ? "/slim" : "/classic");
                        if (!selfSkinData || key != selfSkinKey) {
                            auto sd = std::make_shared<entity::SkinData>();
                            sd->serial = std::hash<std::string>{}(key) | 1ull;
                            sd->skinWidth = ls.skinWidth;
                            sd->skinHeight = ls.skinHeight;
                            sd->skinRgba = ls.skinRgba;
                            sd->capeWidth = ls.capeWidth;
                            sd->capeHeight = ls.capeHeight;
                            sd->capeRgba = ls.capeRgba;
                            sd->slim = ls.slimArms || options.hand.slimArm;
                            selfSkinData = sd;
                            selfSkinKey = key;
                        }
                        self.skin = selfSkinData;
                    }
                    fpSelf = self;
                    if (live && live->RidingUid() != 0)
                        self.vehicleUid = live->RidingUid();
                    dollSelf = self;
                    entities.push_back(std::move(self));
                }
                entity::EntityRenderer::FrameIn ein;
                ein.time = std::chrono::duration<double>(frameStart - startedAt).count();
                ein.dt = dt;
                ein.camera = {camera.x, camera.y, camera.z};
                ein.firstPerson = firstPerson;
                ein.ownNametag = options.thirdPersonNametag;
                ein.frustum = &frustum;
                ein.cameraYaw = camera.yaw - 90.0f;
                ein.cameraPitch = -camera.pitch;
                ein.worldTicks = worldTicks;
                {
                    int fw = 0, fh = 0;
                    platform->GetFramebufferSize(fw, fh);
                    ein.pixelScale = fh > 0 ? 2.0f * std::tan(glm::radians(camera.fov) * 0.5f) / static_cast<float>(fh) : 0.0f;
                    ein.lineWidthPx = std::max(2.5f, static_cast<float>(fw) / 1920.0f * 2.5f);
                    ein.hitboxes = showHitboxes;
                    ein.chunkBorders = showChunkBorders;
                    ein.playerPos = glm::dvec3(camera.x, camera.y - 1.62, camera.z);
                }
                if (gAim.kind != AimHit::Kind::None && options.showHud) {
                    ein.outline = true;
                    ein.outlineLo = gAim.boxLo;
                    ein.outlineHi = gAim.boxHi;
                    ein.outlineShape = gAim.shape;
                }
                if (live)
                    for (const auto& c : live->Cracks())
                        if (!(gLocalCrack.active && c.x == gLocalCrack.x && c.y == gLocalCrack.y && c.z == gLocalCrack.z))
                            ein.cracks.push_back({c.x, c.y, c.z, c.stage});
                if (gLocalCrack.active)
                    ein.cracks.push_back({gLocalCrack.x, gLocalCrack.y, gLocalCrack.z, gLocalCrack.stage});
                if (const char* cr = PERLIN_ENV("PERLIN_PREVIEW_CRACK"); cr && gAim.kind == AimHit::Kind::Block)
                    ein.cracks.push_back({gAim.bx, gAim.by, gAim.bz, std::atoi(cr)});
                ein.groundY = [&](double x, double y, double z) -> double {
                    const int top = entityLevel.FindBlockBelow(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(z)),
                                                               static_cast<int>(std::floor(y)),
                                                               [](uint16_t id) {
                                                                   if (id == 0)
                                                                       return false;
                                                                   const physics::BlockShape& sh = LevelCollision::ShapeOf(id);
                                                                   return sh.count == 1 && sh.boxes[0].x0 <= 0.001 && sh.boxes[0].y0 <= 0.001 &&
                                                                          sh.boxes[0].z0 <= 0.001 && sh.boxes[0].x1 >= 0.999 &&
                                                                          sh.boxes[0].y1 >= 0.999 && sh.boxes[0].z1 >= 0.999;
                                                               });
                    return top <= Chunk::MIN_Y - 1 ? -1000.0 : static_cast<double>(top) + 1.0;
                };
                ein.light = [&](const glm::dvec3& p) -> glm::vec3 {
                    const int bx = static_cast<int>(std::floor(p.x)), by = static_cast<int>(std::floor(p.y)),
                              bz = static_cast<int>(std::floor(p.z));
                    const int64_t key = (static_cast<int64_t>(bx) * 73856093) ^ (static_cast<int64_t>(by) * 19349663) ^ (static_cast<int64_t>(bz) * 83492791);
                    auto& entry = entityLight[key];
                    if (frameStart - entry.at > std::chrono::milliseconds(300)) {
                        entry.at = frameStart;
                        const uint8_t packed = CachedLight(entityLevel, bx, by, bz, frameStart);
                        const int sky = std::clamp<int>(ChunkLighting::Result::Sky(packed), 0, 15);
                        const int blk = std::clamp<int>(ChunkLighting::Result::Block(packed), 0, 15);
                        const uint8_t* px = params.lightmap + (static_cast<size_t>(sky) * kLightmapSize + blk) * 4;
                        entry.colour = {px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f};
                        entry.sky = sky;
                        entry.block = blk;
                    }
                    if (!dyn.empty()) {
                        const int extra = static_cast<int>(dynlight::LevelAt(dyn, p + glm::dvec3(0.0, 0.5, 0.0)));
                        if (extra > entry.block) {
                            const uint8_t* px = params.lightmap + (static_cast<size_t>(entry.sky) * kLightmapSize + extra) * 4;
                            return {px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f};
                        }
                    }
                    return entry.colour;
                };
                if (entityLight.size() > 4096)
                    entityLight.clear();
                {
                    if (!particles.tileFor && levelRenderer) {
                        particles.tileFor = [this](uint16_t id, int bx, int by, int bz, particle::TileChoice& out) {
                            const BlockAppearance& app = levelRenderer->Blocks().Look(id);
                            int tile = app.overlayTile > 0 ? app.bottom : (app.side ? app.side : app.top);
                            if (!tile)
                                tile = app.top ? app.top : app.bottom;
                            if (!tile)
                                return false;
                            out.tile = tile;
                            out.tint = (app.overlayTile <= 0 && (app.tintSide || app.tintTop)) ? glm::vec3(app.tintR, app.tintG, app.tintB)
                                                                                               : glm::vec3(1.0f);
                            if (out.tint != glm::vec3(1.0f) && particleLevel && levelRenderer->Tints().Ready()) {
                                const BiomeTints& tints = levelRenderer->Tints();
                                const std::string name = BlockPalette::Instance().Name(id);
                                const auto has = [&](const char* part) { return name.find(part) != std::string::npos; };
                                if (has("birch_leaves")) {
                                    const auto c = tints.MapAverage("birch");
                                    out.tint = {c[0], c[1], c[2]};
                                } else if (has("spruce_leaves")) {
                                    const auto c = tints.MapAverage("evergreen");
                                    out.tint = {c[0], c[1], c[2]};
                                } else if (has("leaves") || has("vine")) {
                                    glm::vec3 sum(0.0f);
                                    for (int dz = -2; dz <= 2; dz++)
                                        for (int dx = -2; dx <= 2; dx++) {
                                            const auto c = tints.Foliage(tints.NameForId(particleLevel->GetBiome(bx + dx, by, bz + dz)));
                                            sum += glm::vec3(c[0], c[1], c[2]);
                                        }
                                    out.tint = sum / 25.0f;
                                } else if (has("grass") || has("fern") || has("sugar_cane")) {
                                    glm::vec3 sum(0.0f);
                                    for (int dz = -2; dz <= 2; dz++)
                                        for (int dx = -2; dx <= 2; dx++) {
                                            const auto c = tints.Grass(tints.NameForId(particleLevel->GetBiome(bx + dx, by, bz + dz)));
                                            sum += glm::vec3(c[0], c[1], c[2]);
                                        }
                                    out.tint = sum / 25.0f;
                                }
                            }
                            return true;
                        };
                    }
                    static std::vector<int8_t> solidById;
                    particleLevel = &entityLevel;
                    particle::WorldAccess pw;
                    pw.block = [&](int x, int y, int z) -> uint16_t {
                        bool loaded = false;
                        const uint16_t id = entityLevel.GetBlock(x, y, z, loaded);
                        return loaded ? id : uint16_t(0);
                    };
                    pw.solid = [&](int x, int y, int z) {
                        bool loaded = false;
                        const uint16_t id = entityLevel.GetBlock(x, y, z, loaded);
                        if (!loaded || id == 0)
                            return false;
                        if (id >= solidById.size())
                            solidById.resize(static_cast<size_t>(id) + 256, -1);
                        int8_t& s = solidById[id];
                        if (s < 0) {
                            const physics::BlockShape& sh = LevelCollision::ShapeOf(id);
                            s = (sh.count == 1 && sh.boxes[0].x0 <= 0.001 && sh.boxes[0].y0 <= 0.001 && sh.boxes[0].z0 <= 0.001 &&
                                 sh.boxes[0].x1 >= 0.999 && sh.boxes[0].y1 >= 0.999 && sh.boxes[0].z1 >= 0.999)
                                    ? 1
                                    : 0;
                        }
                        return s == 1;
                    };
                    pw.light = ein.light;
                    if (live) {
                        for (const BedrockSession::ParticleEvent& pe : live->TakeParticles()) {
                            const glm::dvec3 at(pe.x, pe.y, pe.z);
                            if (pe.event == 0) {
                                particles.SpawnNamed(pe.name, at);
                            } else if (pe.event == 2001 || pe.event == 2021 || pe.event == 16404) {
                                const int bx = static_cast<int>(std::floor(pe.x)), by = static_cast<int>(std::floor(pe.y)),
                                          bz = static_cast<int>(std::floor(pe.z));
                                const bool mine = lastLocalBreak.x == bx && lastLocalBreak.y == by && lastLocalBreak.z == bz &&
                                                  std::chrono::steady_clock::now() - lastLocalBreak.at < std::chrono::milliseconds(800);
                                if (!mine)
                                    particles.Destroy(bx, by, bz, BlockPalette::Instance().Intern(pe.data));
                            } else {
                                particles.SpawnLevelEvent(pe.event, at, pe.data);
                            }
                        }
                    }
                    if (!live)
                        if (const char* pb = PERLIN_ENV("PERLIN_PREVIEW_BREAK")) {
                            static double burstClock = 0;
                            burstClock += dt;
                            int bx = 0, by = 0, bz = 0;
                            if (burstClock > 0.6 && std::sscanf(pb, "%d,%d,%d", &bx, &by, &bz) == 3) {
                                burstClock = 0;
                                bool loaded = false;
                                const uint16_t id = entityLevel.GetBlock(bx, by, bz, loaded);
                                if (loaded && id)
                                    particles.Destroy(bx, by, bz, id);
                            }
                        }
                    particleClock += std::min(dt, 0.25f);
                    for (int steps = 0; particleClock >= 0.05 && steps < 5; steps++, particleClock -= 0.05)
                        particles.Tick(pw, glm::dvec3(camera.x, camera.y, camera.z));
                    if (particleClock > 0.2)
                        particleClock = 0;
                    particleQuads.clear();
                    particles.Collect(static_cast<float>(particleClock / 0.05), pw, particleQuads);
                    ein.particles = &particleQuads;
                    ein.tilePixels = [this](int tile, std::vector<uint8_t>& rgba) { return levelRenderer->Blocks().TilePixels(tile, rgba); };
                }
                { ProfScope p(3); entityRenderer.Render(entities, ein); }
            }
            if ((live || preview) && camera.mode == PerspectiveMode::FirstPerson && options.showHud &&
                hudState.mode != ui::GameMode::Spectator && ui::UiAssets::Instance().VanillaRoot().size()) {
                HandRenderer::FrameIn handFrame;
                handFrame.partial = partial;
                handFrame.yaw = camera.yaw - 90.0f;
                handFrame.pitch = -camera.pitch;
                {
                    const int sky = std::clamp(handSky, 0, 15);
                    int blk = std::clamp(handBlock, 0, 15);
                    if (!dyn.empty())
                        blk = std::max(blk, static_cast<int>(dynlight::LevelAt(dyn, glm::dvec3(camera.x, camera.y, camera.z))));
                    const uint8_t* px = params.lightmap + (static_cast<size_t>(sky) * kLightmapSize + blk) * 4;
                    handFrame.light = {px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f};
                }
                handFrame.viewEffect = handViewEffect;
                if (session) {
                    handFrame.skin = &session->Skin();
                    handFrame.skinSerial = std::hash<std::string>{}(session->Skin().skinId) * 2 + ((session->Skin().slimArms || options.hand.slimArm) ? 1 : 0);
                    handFrame.slimArm = session->Skin().slimArms;
                }
                handFrame.slimArm = handFrame.slimArm || options.hand.slimArm;
                { ProfScope p(4); handRenderer.Render(handFrame, options.hand); }
                {
                    int sw = 0, sh = 0;
                    renderer->FramebufferSize(sw, sh);
                    handRenderer.RenderScope(sw, sh, partial);
                }
                if (options.hand.bedrockRig && PERLIN_ENV("PERLIN_BEDROCK_RIG") && (fpSelf || previewEntitiesSpawned || true)) {
                    if (!fpSelf) {
                        fpSelf.emplace();
                        fpSelf->runtimeId = 0xFFFFFFFFFFFF0001ull;
                        fpSelf->type = "minecraft:player";
                        fpSelf->isPlayer = fpSelf->isSelf = true;
                        fpSelf->pos = {camera.x, camera.y - entity::kPlayerEye, camera.z};
                        fpSelf->yaw = fpSelf->headYaw = camera.yaw - 90.0f;
                        fpSelf->pitch = -camera.pitch;
                        fpSelf->moveSerial = 1;
                    }
                    const HandRenderer::RigState rs = handRenderer.Rig(partial);
                    fpSelf->mainHand = rs.main;
                    fpSelf->offHand = options.hand.showOffhand ? rs.off : std::string();
                    if (options.hand.leftHanded)
                        std::swap(fpSelf->mainHand, fpSelf->offHand);
                    entity::EntityRenderer::FrameIn fin;
                    fin.time = std::chrono::duration<double>(frameStart - startedAt).count();
                    fin.dt = dt;
                    fin.camera = {camera.x, camera.y, camera.z};
                    fin.firstPerson = true;
                    fin.cameraYaw = camera.yaw - 90.0f;
                    fin.cameraPitch = -camera.pitch;
                    fin.worldTicks = worldTicks;
                    entity::EntityRenderer::FpState fps;
                    fps.attackTime = rs.attackTime;
                    fps.armHeight = rs.armHeight;
                    fps.offArmHeight = rs.offArmHeight;
                    fps.bob = options.hand.viewBobbing && options.hand.bobHand;
                    fps.using_ = rs.using_;
                    fps.blocking = rs.blocking;
                    fps.usedTicks = rs.usedTicks;
                    fps.viewEffect = handFrame.viewEffect;
                    fps.light = handFrame.light;
                    entityRenderer.RenderFirstPerson(*fpSelf, fin, fps);
                }
            }
            {
                static double hudRetry = 0;
                const double nowSeconds = std::chrono::duration<double>(frameStart - startedAt).count();
                const std::string vanillaRoot = options.bedrockSamplesDir + "/repo/resource_pack";
                if (!hud && nowSeconds >= hudRetry) {
                    hudRetry = nowSeconds + 2.0;
                    if (std::filesystem::exists(vanillaRoot + "/ui/hud_screen.json")) {
                        ui::UiAssets::Instance().Configure(vanillaRoot, &levelRenderer->Blocks());
                        auto candidate = std::make_unique<ui::Hud>();
                        if (candidate->Initialize(*renderer, vanillaRoot, paths::DataDir()))
                            hud = std::move(candidate);
                        else
                            candidate->Shutdown();
                    }
                }
                if (hud) {
                    int fbW = 0, fbH = 0;
                    renderer->FramebufferSize(fbW, fbH);
                    ProfScope hudScope(5);
                    hud->Update(hudState, fbW, fbH, options.guiScale);
                    if (!UpdateDisconnectScreen(*hud, fbW, fbH)) {
                        UpdateDeathScreen(*hud, live, hudState, fbW, fbH);
                        UpdateInventoryScreen(*hud, live, hudState, fbW, fbH);
                        UpdatePauseScreen(*hud, live, fbW, fbH);
                        UpdateChatClicks(*hud, live, fbW, fbH);
                    }
                    hud->Render();
                    if (invShown && dollSelf) {
                        const int scale = hud->OverlayScale();
                        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
                        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
                        const float px = static_cast<float>((guiW - ui::kInvPanelW) / 2), py = static_cast<float>((guiH - ui::kInvPanelH) / 2);
                        const float centerX = px + 51.0f, centerY = py + 43.0f;
                        const float f = std::atan((centerX - invMouseX) / 40.0f), g = std::atan((centerY - invMouseY) / 40.0f);
                        entity::Entity doll = *dollSelf;
                        doll.runtimeId = 0xFFFFFFFFFFFF0002ull;
                        doll.pos = {0.0, 0.0, 0.0};
                        doll.yaw = f * 20.0f * 1.0f;
                        doll.headYaw = f * 40.0f;
                        doll.pitch = -g * 20.0f;
                        doll.moveSerial = 1;
                        doll.isSelf = true;
                        doll.flags &= ~entity::flag::Sneaking;
                        entity::EntityRenderer::FrameIn din;
                        din.time = std::chrono::duration<double>(frameStart - startedAt).count();
                        din.dt = dt;
                        din.firstPerson = false;
                        din.camera = {0, 0, 0};
                        const int clip[4] = {static_cast<int>((px + 26) * scale), static_cast<int>((py + 8) * scale),
                                             49 * scale, 70 * scale};
                        entityRenderer.RenderDoll(doll, din, (px + 50.5f) * scale, (py + 43.0f + (1.8f / 2.0f + 0.0625f) * 30.0f) * scale,
                                                  30.0f * scale, clip);
                    }
                    float cx, cy, cw, ch;
                    if (options.showHud && !deathShown && !invShown && !pauseShown && !disconnectShown && (camera.mode == PerspectiveMode::FirstPerson || options.thirdPersonCrosshair) &&
                        hudState.mode != ui::GameMode::Spectator &&
                        hud->Cursor(cx, cy, cw, ch))
                        renderer->DrawHudCursor(cx, cy, cw, ch);
                }
            }
            if (inputManager.Pressed(Action::Screenshot))
                TakeScreenshot();
            { ProfScope p(6); renderer->EndFrame(); }
            if (gProfile.enabled) {
                gProfile.ms[7] += std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count();
                if (++gProfile.frames >= 120) {
                    std::cout << "[profile] ms/frame:";
                    for (int i = 0; i < 8; i++)
                        std::cout << ' ' << gProfile.names[i] << '=' << gProfile.ms[i] / gProfile.frames;
                    std::cout << std::endl;
                    for (double& m : gProfile.ms)
                        m = 0;
                    gProfile.frames = 0;
                }
            }

            if (frameStart - animationAt >= kAnimationStep) {
                animationAt = frameStart;
                levelRenderer->TickAnimation(++animationStep);
            }

            frames++;
            const auto now = Clock::now();
            if (now - titleAt >= kTitleInterval) {
                const double seconds = std::chrono::duration<double>(now - titleAt).count();
                fpsShown = static_cast<int>(frames / seconds + 0.5);
                UpdateTitle(fpsShown);
                titleAt = now;
                frames = 0;
            }

            if (!options.vsync && options.maxFps > 0) {
                const auto target = frameStart + std::chrono::microseconds(1000000 / options.maxFps);
                std::this_thread::sleep_until(target);
            }
        }

        audioEngine.Close();
        StopSampleProfiler();
        entityRenderer.Shutdown();
        handRenderer.Shutdown();
        if (hud)
            hud->Shutdown();
        hud.reset();
        levelRenderer.reset();
        renderer->Shutdown();
    }
}

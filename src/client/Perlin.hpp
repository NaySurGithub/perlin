#pragma once
#include "client/audio/SoundSystem.hpp"
#include "client/audio/Subtitles.hpp"
#include <chrono>
#include "client/Camera.hpp"
#include "client/Options.hpp"
#include "client/entity/EntityRenderer.hpp"
#include "client/hand/HandRenderer.hpp"
#include "client/input/Input.hpp"
#include "client/input/InputManager.hpp"
#include "client/multiplayer/BedrockSession.hpp"
#include "client/particle/ParticleSystem.hpp"
#include "client/player/LocalPlayer.hpp"
#include "client/renderer/LevelRenderer.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/Hud.hpp"
#include "util/ThreadPool.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

class AppPlatform;

namespace perlin {
    struct LaunchArgs {
        std::string server;
        std::string playerName;
        std::string skin;
        std::string cape;
        bool slimArms = false;
        bool login = false;
        bool offline = false;
        std::string transport;
        bool logout = false;
    };

    class Perlin {
    public:
        explicit Perlin(LaunchArgs args) : args(std::move(args)) {}

        bool Initialize(AppPlatform* platform);
        void Shutdown();

        Input& GetInput() { return input; }

    private:
        void RenderThreadMain();
        void FillHudState(ui::HudState& state, BedrockSession* live, bool preview);
        void Connect();
        bool AcceptEula();
        void TakeScreenshot();
        void UpdateDeathScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW, int fbH);
        bool deathShown = false, deathLeftDown = false, deathRespawnSent = false, deathTextMode = false;
        std::chrono::steady_clock::time_point deathAt{}, deathSentAt{};
        std::string deathSignature, deathTemplate;
        uint64_t deathTemplateVersion = 0;
        bool deathHoverLogged = false;
        std::chrono::steady_clock::time_point deathCheckAt{};
        void UpdateCreativeScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW, int fbH);
        void UpdateInventoryScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW, int fbH);
        bool invShown = false, invLeftDown = false, invRightDown = false, invPreviewOpened = false;
        float invMouseX = 0, invMouseY = 0;
        int creativeTab = 0;
        bool creativeDollVisible = false;
        float creativeScroll = 0.0f;
        bool creativeDragging = false;
        std::string creativeSearch, creativeSignature, creativeTemplate;
        uint64_t creativeTemplateVersion = 0;
        std::optional<entity::Entity> dollSelf;
        int invHoverAddr = -1;
        const Level* particleLevel = nullptr;
        int invPicked = -1, invHover = -1;
        std::chrono::steady_clock::time_point invOpenedAt{}, invCheckAt{};
        std::string invSignature, invTemplate;
        uint64_t invTemplateVersion = 0;
        void UpdatePauseScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        bool UpdateDisconnectScreen(ui::Hud& hud, int fbW, int fbH);
        bool disconnectShown = false, disconnectLeftDown = false, disconnectTextMode = false;
        std::chrono::steady_clock::time_point disconnectOpenedAt{}, disconnectCheckAt{};
        std::string disconnectSignature, disconnectTemplate;
        uint64_t disconnectTemplateVersion = 0;
        bool pauseShown = false, pauseLeftDown = false, pausePreviewOpened = false;
        std::chrono::steady_clock::time_point pauseOpenedAt{}, pauseCheckAt{};
        std::string pauseSignature, pauseTemplate;
        uint64_t pauseTemplateVersion = 0;
        void UpdateChatClicks(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        bool chatLeftDown = false;
        uint32_t commandsSeen = 0;
        std::chrono::steady_clock::time_point playersAt{};
        void UpdateTitle(int fps);
        void FillDebug(ui::HudState& state, BedrockSession* live, Level* level);
        bool debugOn = false;
        int fpsShown = 0;
        uint64_t rateTx = 0, rateRx = 0;
        int txRate = 0, rxRate = 0;
        std::chrono::steady_clock::time_point rateAt{};
        int heightSurface = 0;
        int dbgSky = 15, dbgBlock = 0;
        std::chrono::steady_clock::time_point heightAt{};

        LaunchArgs args;
        AppPlatform* platform = nullptr;
        Options options;
        Input input;
        InputManager inputManager{input, options};

        std::unique_ptr<RenderContext> renderer;
        std::unique_ptr<LevelRenderer> levelRenderer;
        std::unique_ptr<ui::Hud> hud;
        ui::HudState hudState;
        HandRenderer handRenderer;
        entity::EntityRenderer entityRenderer;
        particle::ParticleSystem particles;
        double particleClock = 0;
        std::vector<particle::Quad> particleQuads;
        struct LocalBreak {
            int x = 0, y = 0, z = 0;
            std::chrono::steady_clock::time_point at{};
        } lastLocalBreak;
        audio::AudioEngine audioEngine;
        audio::SoundSystem sounds{audioEngine};
        audio::Subtitles subtitles;
        void UpdateAudio(BedrockSession* live, double now, float dt);
        entity::EntityTracker previewEntities;
        bool previewEntitiesSpawned = false;
        std::vector<uint64_t> previewWalk;
        void SpawnPreviewEntities();
        std::shared_ptr<const entity::SkinData> selfSkinData;
        std::string selfSkinKey;
        struct EntityLightEntry {
            std::chrono::steady_clock::time_point at{};
            glm::vec3 colour{1.0f};
            int sky = 0, block = 0;
        };
        std::unordered_map<int64_t, EntityLightEntry> entityLight;
        ui::ChatLog chat;
        std::chrono::steady_clock::time_point chatOpenedAt{};
        struct HudAnim {
            uint64_t ticks = 0;
            int toolTimer = 0;
            ui::HudItem lastTool;
            std::chrono::steady_clock::time_point titleAtSeen{}, actionAtSeen{};
            int overlayTime = 0, titleTime = 0, titleFadeIn = 10, titleStay = 70, titleFadeOut = 20;
            bool haveHealth = false;
            int lastHealth = 0;
            int lastHealthTick = 0, lastHurtTick = -1000;
            int blinkUntil = 0, displayHealth = 0;
        } hudAnim;
        float hudPartial = 0.0f;
        bool headUnderwater = false;
        int previewSlot = 0;
        void TickHud();
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
        std::string statusHint;
        void SetStatusHint(const std::string& text);
        bool onlineThisRun = false;
        std::atomic<bool> sessionReady{false};
    };
}

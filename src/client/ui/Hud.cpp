#include "client/ui/Hud.hpp"
#include "client/ui/GuiScale.hpp"

#include "client/ui/HudRenderers.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/UiAssets.hpp"
#include "client/ui/JavaDefaults.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"
#include "client/resources/ServerPacks.hpp"
#include "client/resources/TexturePack.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace perlin::ui {
    namespace {
        double Now() {
            using Clock = std::chrono::steady_clock;
            static const auto start = Clock::now();
            return std::chrono::duration<double>(Clock::now() - start).count();
        }

        bool Write(const fs::path& path, const std::string& text) {
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream f(path, std::ios::binary);
            if (!f)
                return false;
            f << text;
            return true;
        }
    }

    std::string ExtractPackUi(const std::string& dataDir, const std::string& folder) {
        TexturePack& tp = TexturePack::Instance();
        const fs::path root = fs::path(dataDir) / "cache" / folder;
        std::error_code ec;
        fs::remove_all(root, ec);
        if (!tp.Active())
            return {};
        const std::vector<std::string> files = tp.ListFiles("ui", ".json");
        bool any = false;
        for (const std::string& rel : files) {
            const std::vector<std::string> layers = tp.ReadTextLayers(rel);
            if (layers.empty())
                continue;
            const fs::path out = root / rel;
            fs::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            if (!f)
                continue;
            f << layers.front();
            any = true;
        }
        return any ? root.string() : std::string();
    }

    Hud::Hud() = default;
    Hud::~Hud() = default;

    void Hud::WriteDefaults() {
        Write(fs::path(dataDir) / "ui_defaults" / "_perlin_java.json", kJavaHudOverrides);
    }

    bool Hud::Initialize(RenderContext& renderer, const std::string& vanilla, const std::string& data_dir) {
        dataDir = data_dir;
        vanillaRoot = vanilla;
        if (!backend.Initialize(renderer))
            return false;
        WriteDefaults();
        std::cout << "[ui] font: " << UiAssets::Instance().LoadFont(dataDir) << std::endl;
        std::error_code ec;
        fs::create_directories(fs::path(dataDir) / "ui", ec);

        services.textWidth = [](const std::string& text, float s) { return McFont::Instance().Width(text, s); };
        services.lineHeight = [](float s) { return static_cast<float>(McFont::kLineHeight) * s; };
        services.textureSize = [](const std::string& tex, int& w, int& h) { return UiAssets::Instance().ImageSize(tex, w, h); };
        services.localize = [](const std::string& key) { return UiAssets::Instance().Localize(key); };

        data = std::make_unique<HudData>(state);
        RegisterHudRenderers(emitter, state, services);
        return Rebuild();
    }

    void Hud::Shutdown() {
        backend.Shutdown();
        tree.reset();
    }

    bool Hud::Rebuild() {
        {
            std::vector<std::string> roots;
            const std::vector<std::string> server = ServerPacks::Instance().Directories();
            for (size_t i = server.size(); i-- > 0;)
                roots.push_back(server[i] + "/ui");
            const std::string packUi = ExtractPackUi(dataDir);
            if (!packUi.empty())
                roots.push_back(packUi + "/ui");
            roots.push_back(dataDir + "/ui");
            roots.push_back(dataDir + "/ui_defaults");
            roots.push_back(vanillaRoot + "/ui");
            library.SetRoots(std::move(roots));
        }
        if (!library.Scan()) {
            std::cout << "[ui] no ui folder found under " << vanillaRoot << "/ui (waiting for the Bedrock samples)" << std::endl;
            tree.reset();
            return false;
        }
        BuildReport report;
        auto built = BuildTree(library, "hud.hud_screen", EngineRootVariables(), report);
        for (const std::string& problem : library.Problems())
            std::cout << "[ui] " << problem << std::endl;
        for (const std::string& missing : report.missingControls)
            std::cout << "[ui] missing control " << missing << std::endl;
        if (!built) {
            std::cout << "[ui] hud.hud_screen could not be built" << std::endl;
            return false;
        }
        tree = std::move(built);
        {
            BuildReport crossReport;
            crosshairTree = BuildTree(library, "hud_crosshair.hud_crosshair_screen", EngineRootVariables(), crossReport);
            cursorEmitter.Register("cursor_renderer", [this](const LNode& n, RmlWriter&) {
                cursorX = std::floor(n.ax + (n.w - 15.0f) * 0.5f);
                cursorY = std::floor(n.ay + (n.h - 15.0f) * 0.5f);
                cursorValid = true;
            });
            cursorValid = false;
        }
        lastSignature = 0;
        lastRcss.clear();
        rebuilds++;
        std::cout << "[ui] hud built: " << report.nodes << " controls from " << library.FileCount() << " files" << std::endl;
        return true;
    }

    std::string Hud::ReadUserRcss() const {
        std::ifstream f(dataDir + "/ui/hud.rcss", std::ios::binary);
        if (!f)
            return {};
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    int Hud::Update(const HudState& incoming, int fbW, int fbH, int guiScaleOption) {
        if (!backend.Ready())
            return 1;
        const uint64_t incomingSignature = HudSignature(incoming);
        const bool stateChanged = incomingSignature != lastIncomingSignature;
        lastIncomingSignature = incomingSignature;
        if (stateChanged)
            state = incoming;

        scale = JavaGuiScale(fbW, fbH, guiScaleOption);
        const float newW = std::ceil(static_cast<float>(fbW) / scale), newH = std::ceil(static_cast<float>(fbH) / scale);
        const bool resized = newW != guiW || newH != guiH || fbW != lastFbW || fbH != lastFbH;
        guiW = newW;
        guiH = newH;
        state.guiW = guiW;
        state.guiH = guiH;
        if (resized)
            lastIncomingSignature = 0;
        lastFbW = fbW;
        lastFbH = fbH;

        const double now = Now();
        bool rebuilt = false;
        if (reloadRequested || now - sinceCheck > 2.0) {
            sinceCheck = now;
            std::string freshRcss = ReadUserRcss();
            if (freshRcss != rcssCache) {
                rcssCache = std::move(freshRcss);
                lastSignature = 0;
            }
            if (reloadRequested || library.Changed()) {
                reloadRequested = false;
                UiAssets::Instance().ClearItemCache();
                UiAssets::Instance().LoadFont(dataDir);
                backend.ReloadTextures();
                Rebuild();
                rebuilt = true;
            }
        }
        if (!tree)
            return scale;
        if (now - templateCheck > 2.0 || templatesStale) {
            templateCheck = now;
            uint64_t vh = 0, vc = 0;
            std::string hudT = LoadUiTemplate(dataDir, "hud_screen.rml", DefaultHudTemplate(), vh);
            std::string chatT = LoadUiTemplate(dataDir, "chat_screen.rml", DefaultChatTemplate(), vc);
            if (vh != hudTemplateVersion || vc != chatTemplateVersion || templatesStale) {
                hudTemplateVersion = vh;
                chatTemplateVersion = vc;
                emitter.SetTemplates(std::move(hudT), std::move(chatT));
                lastSignature = 0;
                templatesStale = false;
                std::cout << "[ui] loaded " << dataDir << "/ui/hud_screen.rml and chat_screen.rml" << std::endl;
            }
        }

        if (crosshairTree && (!cursorValid || resized || rebuilt)) {
            cursorValid = false;
            auto crossEvaluated = EvaluateTree(*crosshairTree, *data, services, guiW, guiH);
            cursorEmitter.Emit(*crossEvaluated, guiW, guiH, std::string(), false);
        }
        if (!cursorValid) {
            cursorX = std::floor((guiW - 15.0f) * 0.5f);
            cursorY = std::floor((guiH - 15.0f) * 0.5f);
            cursorValid = true;
        }

        const std::string& rcss = rcssCache;
        const uint64_t signature = stateChanged || resized || rebuilt ? HudSignature(state) : lastSignature;
        if (!rebuilt && !resized && signature == lastSignature && rcss == lastRcss && fontVersion == McFont::Instance().Version())
            return scale;
        lastSignature = signature;
        lastRcss = rcss;
        fontVersion = McFont::Instance().Version();

        auto evaluated = EvaluateTree(*tree, *data, services, guiW, guiH);
        lastRml = emitter.Emit(*evaluated, guiW, guiH, rcss, false);
        backend.Resize(static_cast<int>(guiW), static_cast<int>(guiH), scale);
        std::string error;
        if (!backend.SetDocument(lastRml, error)) {
            std::cout << "[ui] " << error << std::endl;
            if (!emitterDefault) {
                emitterDefault = true;
                emitter.SetTemplates(std::string(), std::string());
                lastRml = emitter.Emit(*evaluated, guiW, guiH, std::string(), false);
                backend.SetDocument(lastRml, error);
            }
        } else {
            emitterDefault = false;
        }
        if (!dumped || rebuilt) {
            dumped = true;
            Write(fs::path(dataDir) / "ui_generated" / "hud.rml", lastRml);
            for (const std::string& u : emitter.UnknownRenderers())
                std::cout << "[ui] custom renderer not implemented: " << u << std::endl;
        }
        backend.Update();
        return scale;
    }

    void Hud::UpdateOverlay(int fbW, int fbH, int guiScaleOption) {
        if (!backend.Ready())
            return;
        const int s = JavaGuiScale(fbW, fbH, guiScaleOption);
        backend.Resize(static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(s))),
                       static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(s))), s);
        backend.Update();
    }

    void Hud::Render() {
        if (!backend.Ready() || (!tree && !backend.HasOverlay()))
            return;
        backend.Render();
    }
}

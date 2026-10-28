#pragma once
#include "client/ui/HudState.hpp"
#include "client/ui/Json.hpp"
#include "client/ui/JsonUi.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiLayout.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlin::ui {
    // Prefix the emitter puts on every JSON UI button that is not a form button: "jb:<to_button_id>[:<row>]".
    inline constexpr const char* kJsonButtonPrefix = "jb:";

    // A clicked JSON UI button, split back into the Bedrock button id and the collection row it came from.
    struct JsonButton {
        std::string id;  // e.g. "button.menu_continue"
        int index = -1;
    };
    bool ParseJsonButton(const std::string& hoverId, JsonButton& out);

    // Data a screen hands to its JSON UI: global bindings ("#name"), collections (rows of bindings) and
    // factory items. Any binding left unset keeps the control's own default, exactly like the game does
    // for bindings a screen controller does not answer.
    class ScreenData : public UiData {
    public:
        using Row = std::unordered_map<std::string, Json>;

        void Set(const std::string& name, Json value) { globals[name] = std::move(value); }
        void Set(const std::string& name, bool value) { globals[name] = Json(value); }
        void Set(const std::string& name, const char* value) { globals[name] = Json(std::string(value)); }
        void Set(const std::string& name, const std::string& value) { globals[name] = Json(value); }
        void Set(const std::string& name, double value) { globals[name] = Json(value); }
        std::vector<Row>& Collection(const std::string& name) { return collections[name]; }
        std::vector<FactoryItem>& FactoryItems(const std::string& name) { return factories[name]; }
        // Answers bindings nothing else set (used for families like "#*_visible").
        std::function<bool(const std::string& name, Json& out)> fallback;

        bool Global(const std::string& name, Json& out) const override;
        int CollectionSize(const std::string& collection) const override;
        bool CollectionValue(const std::string& collection, int index, const std::string& binding, Json& out) const override;
        std::vector<FactoryItem> Factory(const std::string& name) const override;

        // Stable digest of everything above, so callers only re-emit when something changed.
        std::string Signature() const;

    private:
        std::unordered_map<std::string, Json> globals;
        std::unordered_map<std::string, std::vector<Row>> collections;
        std::unordered_map<std::string, std::vector<FactoryItem>> factories;
    };

    // Builds full screens ("pause.pause_screen", "start.start_screen", ...) from the active JSON UI stack:
    // server packs first, then resource packs, then the vanilla Bedrock files. One instance serves every screen,
    // so a server that overrides any vanilla screen gets it drawn the way it designed it.
    class JsonScreens {
    public:
        static JsonScreens& Instance();
        ~JsonScreens();

        // Rescans the UI stack when the server pack list or resource packs changed. Cheap when nothing did.
        void Warm(const std::vector<std::string>& serverPackDirs);
        bool Ready() const { return state.load() == 2; }
        bool WaitReady(int timeoutMs);
        // Scan generation; bumps each time the stack was rescanned, so callers can drop cached documents.
        // Changes whenever cached documents must be rebuilt: the stack was rescanned or the language changed.
        uint64_t Generation() const;

        // Emits `reference` as an RML document. Returns false (and leaves `rml` alone) when the screen does not
        // exist in the stack or failed to build, so callers can fall back to their built-in layout.
        // `rootVars` are the engine variables a screen is built with (e.g. $container_title); trees are cached per set.
        bool Build(const std::string& reference, const ScreenData& data, int guiW, int guiH, float scroll, std::string& rml,
                   const Vars* rootVars = nullptr);
        // Items drawn by inventory_item_renderer, per collection ("container_items", "inventory_items", "hotbar_items").
        void SetItems(std::unordered_map<std::string, std::vector<HudItem>> table) { items = std::move(table); }
        // True when some pack in the stack (not just vanilla) defines this screen's namespace.
        bool Overridden(const std::string& ns) const;
        float ContentHeight() const { return emitter.ContentHeight(); }
        float ViewportHeight() const { return emitter.ViewportHeight(); }
        // True while the last built screen has an animation still running, so callers re-evaluate it each frame.
        bool Animating() const { return anim.animating; }
        RmlEmitter& Emitter() { return emitter; }
        // UI texture key (see UiAssets::SetHead) that paper_doll_renderer draws: the player's 3D skin.
        void SetPaperDoll(std::string key) { paperDoll = std::move(key); }
        // Where the last emitted document drew the doll (gui units); false if it drew none.
        bool PaperDollRect(float& x, float& y, float& w, float& h) const {
            x = dollX; y = dollY; w = dollW; h = dollH;
            return dollW > 0;
        }
        UiServices& Services() { return services; }

    private:
        JsonScreens();
        const Node* Tree(const std::string& reference, const Vars* rootVars);

        UiLibrary library;
        std::unordered_map<std::string, std::unique_ptr<Node>> trees;
        std::unordered_set<std::string> failed;
        std::unordered_set<std::string> overriddenNs;
        RmlEmitter emitter;
        UiServices services;
        AnimHost anim;
        std::unordered_map<std::string, Json> animCache;
        std::thread worker;
        std::atomic<int> state{0};
        std::atomic<uint64_t> generation{0};
        std::string key;
        std::string paperDoll;
        std::unordered_map<std::string, std::vector<HudItem>> items;
        float dollX = 0, dollY = 0, dollW = 0, dollH = 0;
        std::mutex mutex;
    };
}

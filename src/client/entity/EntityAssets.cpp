#include <functional>
#include "client/entity/EntityAssets.hpp"

#include "client/ui/Json.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace perlin::entity {
    namespace fs = std::filesystem;
    using Json = ui::Json;
    using molang::Expr;
    using molang::Value;

    std::string LowerCase(std::string s) {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    std::string AfterDot(const std::string& ref) {
        const size_t d = ref.find('.');
        return d == std::string::npos ? ref : ref.substr(d + 1);
    }

    namespace {
        bool ReadAll(const fs::path& p, std::string& out) {
            std::ifstream f(p, std::ios::binary);
            if (!f)
                return false;
            std::stringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return true;
        }

        std::vector<fs::path> JsonFiles(const fs::path& dir) {
            std::vector<fs::path> files;
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(dir, ec))
                if (e.is_regular_file() && e.path().extension() == ".json")
                    files.push_back(e.path());
            const auto legacy = [](const fs::path& p) { return p.filename().string().find(".v1.") != std::string::npos; };
            std::sort(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) {
                if (legacy(a) != legacy(b))
                    return legacy(a);
                return a < b;
            });
            return files;
        }

        ExprPtr ParseValue(const Json& v) {
            if (v.IsString())
                return Expr::Parse(v.string);
            if (v.IsNumber())
                return Expr::Constant(v.number);
            if (v.IsBool())
                return Expr::Constant(v.boolean ? 1.0 : 0.0);
            return nullptr;
        }

        void ParseEntity(const Json& doc, std::unordered_map<std::string, ClientEntityDef>& out) {
            const Json* client = doc.Find("minecraft:client_entity");
            const Json* desc = client ? client->Find("description") : nullptr;
            if (!desc || !desc->Find("identifier"))
                return;
            ClientEntityDef d;
            d.identifier = desc->Find("identifier")->StringOr("");
            const auto readMap = [&](const char* key, std::map<std::string, std::string>& dest) {
                if (const Json* m = desc->Find(key); m && m->IsObject())
                    for (const auto& [k, v] : m->object)
                        if (v.IsString())
                            dest[LowerCase(k)] = v.string;
            };
            readMap("textures", d.textures);
            readMap("geometry", d.geometries);
            readMap("animations", d.animations);
            readMap("materials", d.materials);
            if (const Json* scripts = desc->Find("scripts")) {
                const auto readScripts = [&](const char* key, std::vector<ExprPtr>& dest) {
                    if (const Json* l = scripts->Find(key); l && l->IsArray())
                        for (const Json& s : l->array)
                            if (s.IsString())
                                dest.push_back(Expr::Parse(s.string));
                };
                readScripts("initialize", d.initialize);

                if (const Json* l = scripts->Find("pre_animation"); l && l->IsArray()) {
                    std::string joined;
                    for (const Json& s : l->array)
                        if (s.IsString())
                            joined += s.string + "\n";
                    d.preAnimation.push_back(Expr::Parse(joined));
                }
                if (const Json* l = scripts->Find("animate"); l && l->IsArray())
                    for (const Json& a : l->array) {
                        if (a.IsString())
                            d.animate.push_back({LowerCase(a.string), nullptr});
                        else if (a.IsObject())
                            for (const auto& [target, cond] : a.object)
                                d.animate.push_back({LowerCase(target), ParseValue(cond)});
                    }
                if (const Json* s = scripts->Find("scale"))
                    d.scale = ParseValue(*s);
            }
            if (const Json* rcs = desc->Find("render_controllers"); rcs && rcs->IsArray())
                for (const Json& r : rcs->array) {
                    if (r.IsString())
                        d.renderControllers.push_back({r.string, nullptr});
                    else if (r.IsObject())
                        for (const auto& [id, cond] : r.object)
                            d.renderControllers.push_back({id, ParseValue(cond)});
                }
            d.ok = !d.geometries.empty() || !d.renderControllers.empty();
            if (d.ok)
                out[d.identifier] = std::move(d);
        }

        void ParseRenderControllers(const Json& doc, std::unordered_map<std::string, RenderControllerDef>& out) {
            const Json* list = doc.Find("render_controllers");
            if (!list || !list->IsObject())
                return;
            for (const auto& [id, def] : list->object) {
                RenderControllerDef rc;
                if (const Json* g = def.Find("geometry"); g && g->IsString())
                    rc.geometry = Expr::Parse(g->string);
                if (const Json* t = def.Find("textures"); t && t->IsArray())
                    for (const Json& e : t->array)
                        if (e.IsString())
                            rc.textures.push_back(Expr::Parse(e.string));
                if (const Json* arrays = def.Find("arrays"); arrays && arrays->IsObject())
                    for (const auto& [kind, group] : arrays->object)
                        if (group.IsObject())
                            for (const auto& [name, values] : group.object)
                                if (values.IsArray())
                                    for (const Json& v : values.array)
                                        rc.arrays[LowerCase(name)].push_back(Value::Str(LowerCase(v.StringOr(""))));
                if (const Json* parts = def.Find("part_visibility"); parts && parts->IsArray())
                    for (const Json& entry : parts->array)
                        if (entry.IsObject())
                            for (const auto& [pattern, expr] : entry.object)
                                rc.parts.push_back({LowerCase(pattern), ParseValue(expr)});
                if (const Json* c = def.Find("color"); c && c->IsObject()) {
                    rc.hasColor = true;
                    static const char* kKeys[4] = {"r", "g", "b", "a"};
                    for (int i = 0; i < 4; i++)
                        if (const Json* v = c->Find(kKeys[i]))
                            rc.color[i] = ParseValue(*v);
                }
                if (const Json* m = def.Find("materials"); m && m->IsArray())
                    for (const Json& e : m->array)
                        if (e.IsObject())
                            for (const auto& [k, v] : e.object) {
                                rc.materials.push_back({LowerCase(k), ParseValue(v)});
                                const std::string mat = LowerCase(v.StringOr(""));
                                if (mat.find("alpha") != std::string::npos || mat.find("translucent") != std::string::npos ||
                                    mat.find("spectator") != std::string::npos || mat.find("ghost") != std::string::npos)
                                    rc.translucent = true;
                            }
                out[id] = std::move(rc);
            }
        }
    }

    void EntityAssets::Configure(const std::string& newRoot) {
        if (loaded && root == newRoot)
            return;
        root = newRoot;
        loaded = false;
        entities.clear();
        controllers.clear();
        animations = attachable::AnimationLibrary();
        geometries = attachable::GeometryLibrary();
        if (root.empty())
            return;
        materialLibrary.Load(root);
        LoadAll();
        loaded = true;
    }

    void EntityAssets::LoadAll() {
        const auto start = std::chrono::steady_clock::now();
        std::string text;
        for (const fs::path& p : JsonFiles(fs::path(root) / "models" / "entity"))
            if (ReadAll(p, text))
                geometries.Load(text);
        if (ReadAll(fs::path(root) / "models" / "mobs.json", text))
            geometries.Load(text);
        for (const fs::path& p : JsonFiles(fs::path(root) / "animations"))
            if (ReadAll(p, text))
                animations.LoadAnimations(text);
        for (const fs::path& p : JsonFiles(fs::path(root) / "animation_controllers"))
            if (ReadAll(p, text))
                animations.LoadControllers(text);
        for (const fs::path& p : JsonFiles(fs::path(root) / "render_controllers")) {
            Json doc;
            std::string error;
            if (ReadAll(p, text) && Json::Parse(text, doc, error))
                ParseRenderControllers(doc, controllers);
        }
        for (const fs::path& p : JsonFiles(fs::path(root) / "entity")) {
            Json doc;
            std::string error;
            if (ReadAll(p, text) && Json::Parse(text, doc, error))
                ParseEntity(doc, entities);
        }

        {
            std::function<const Json*(const Json&)> findBox = [&](const Json& j) -> const Json* {
                if (j.IsObject()) {
                    if (const Json* box = j.Find("minecraft:collision_box"); box && box->IsObject())
                        return box;
                    for (const auto& [k, v] : j.object)
                        if (const Json* r = findBox(v))
                            return r;
                }
                return nullptr;
            };
            for (const fs::path& p : JsonFiles(fs::path(root).parent_path() / "behavior_pack" / "entities")) {
                Json doc;
                std::string error;
                if (!ReadAll(p, text) || !Json::Parse(text, doc, error))
                    continue;
                const Json* entity = doc.Find("minecraft:entity");
                const Json* desc = entity ? entity->Find("description") : nullptr;
                const Json* id = desc ? desc->Find("identifier") : nullptr;
                if (!id || !id->IsString() || !entity)
                    continue;
                if (const Json* box = findBox(*entity)) {
                    const Json* w = box->Find("width");
                    const Json* hgt = box->Find("height");
                    if (w && hgt && w->IsNumber() && hgt->IsNumber())
                        collisionBoxes[id->string] = {static_cast<float>(w->number), static_cast<float>(hgt->number)};
                }
            }
        }

        for (const auto& [id, def] : entities)
            for (const auto& ref : def.renderControllers)
                if (!controllers.count(ref.id)) {
                    RenderControllerDef rc;
                    rc.geometry = Expr::Parse("Geometry.default");
                    rc.textures.push_back(Expr::Parse("Texture.default"));
                    controllers[ref.id] = std::move(rc);
                }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "[entity] loaded " << entities.size() << " entity definitions, " << geometries.Size() << " geometries, "
                  << controllers.size() << " render controllers in " << static_cast<int>(ms) << " ms" << std::endl;
    }

    const ClientEntityDef* EntityAssets::Entity(const std::string& identifier) const {
        const auto it = entities.find(identifier);
        return it == entities.end() ? nullptr : &it->second;
    }

    bool EntityAssets::CollisionBox(const std::string& identifier, float& width, float& height) const {
        const auto it = collisionBoxes.find(identifier);
        if (it == collisionBoxes.end())
            return false;
        width = it->second.first;
        height = it->second.second;
        return true;
    }

    const RenderControllerDef* EntityAssets::RenderController(const std::string& id) const {
        const auto it = controllers.find(id);
        return it == controllers.end() ? nullptr : &it->second;
    }
}

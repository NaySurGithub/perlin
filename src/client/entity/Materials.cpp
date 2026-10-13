#include "client/entity/Materials.hpp"

#include "client/ui/Json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace perlin::entity {
    namespace fs = std::filesystem;
    using Json = ui::Json;

    namespace {
        std::string Lower(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
    }

    MaterialInfo MaterialLibrary::Builtin(const std::string& rawName) {
        const std::string name = Lower(rawName);
        MaterialInfo m;

        if (Has(name, "additive")) {
            m.blend = MaterialInfo::Blend::Additive;
            m.depthWrite = false;
            m.hurtTint = false;
            m.emissive = 2;
        } else if (Has(name, "emissive_alpha") || name == "spider" || name == "enderman" || name == "enderman_invisible") {
            m.blend = MaterialInfo::Blend::Alpha;
            m.emissive = 2;
            m.depthWrite = false;
            m.hurtTint = false;
        } else if (Has(name, "emissive")) {
            m.blend = MaterialInfo::Blend::Opaque;
            m.emissive = 1;
        } else if (Has(name, "alphablend") || Has(name, "translucent") || Has(name, "ghost") || Has(name, "spectator") ||
                   Has(name, "invisible") || Has(name, "glass") || Has(name, "shield_glint") || Has(name, "slime_outer") ||
                   name == "drowned" || Has(name, "blend")) {
            m.blend = MaterialInfo::Blend::Alpha;
            m.depthWrite = Has(name, "alphablend") ? false : true;
        } else if (Has(name, "alphatest")) {
            m.blend = MaterialInfo::Blend::AlphaTest;
        } else if (name == "entity" || name == "entity_static" || name == "entity_multitexture" || name == "entity_change_color" ||
                   name == "entity_lightning" || name == "entity_custom") {

            m.blend = MaterialInfo::Blend::AlphaTest;
        }
        if (Has(name, "nocull") || Has(name, "double_sided") || Has(name, "two_sided"))
            m.noCull = true;
        if (Has(name, "lightning") || Has(name, "fireball") || Has(name, "beam") || Has(name, "dragon_explosion"))
            m.hurtTint = false;
        return m;
    }

    void MaterialLibrary::ApplyJson(const std::string& text, std::unordered_map<std::string, MaterialInfo>& out,
                                    const std::unordered_map<std::string, MaterialInfo>& known) {
        Json doc;
        std::string error;
        if (!Json::Parse(text, doc, error))
            return;
        const Json* list = doc.Find("materials");
        if (!list || !list->IsObject())
            return;

        std::vector<std::pair<std::string, const Json*>> pending;
        for (const auto& [key, value] : list->object)
            if (key != "version" && value.IsObject())
                pending.emplace_back(key, &value);
        for (size_t pass = 0; pass < 6 && !pending.empty(); pass++) {
            std::vector<std::pair<std::string, const Json*>> next;
            for (const auto& [key, def] : pending) {
                const size_t colon = key.find(':');
                const std::string name = Lower(key.substr(0, colon));
                const std::string parent = colon == std::string::npos ? "" : Lower(key.substr(colon + 1));
                MaterialInfo info = Builtin(name);
                if (!parent.empty()) {
                    if (const auto it = out.find(parent); it != out.end())
                        info = it->second;
                    else if (const auto it2 = known.find(parent); it2 != known.end())
                        info = it2->second;
                    else if (pass + 1 < 6) {

                        bool inFile = false;
                        for (const auto& [k2, v2] : pending)
                            if (Lower(k2.substr(0, k2.find(':'))) == parent)
                                inFile = true;
                        if (inFile) {
                            next.emplace_back(key, def);
                            continue;
                        }
                        info = Builtin(parent);
                    } else {
                        info = Builtin(parent);
                    }
                }
                if (const Json* states = def->Find("states"); states && states->IsArray())
                    for (const Json& s : states->array) {
                        const std::string st = s.StringOr("");
                        if (st == "Blending") info.blend = MaterialInfo::Blend::Alpha;
                        else if (st == "DisableCulling") info.noCull = true;
                        else if (st == "DisableDepthWrite") info.depthWrite = false;
                    }
                if (const Json* defines = def->Find("defines"); defines && defines->IsArray())
                    for (const Json& d : defines->array) {
                        const std::string df = d.StringOr("");
                        if (df == "ALPHA_TEST" && info.blend == MaterialInfo::Blend::Opaque)
                            info.blend = MaterialInfo::Blend::AlphaTest;
                        else if (df == "USE_EMISSIVE")
                            info.emissive = std::max<uint8_t>(info.emissive, 1);
                        else if (df == "USE_OVERLAY")
                            info.hurtTint = true;
                        else if (df == "NO_ALPHA_TEST" || df == "OPAQUE")
                            info.blend = MaterialInfo::Blend::Opaque;
                    }
                const std::string src = def->Find("blendSrc") ? def->Find("blendSrc")->StringOr("") : "";
                const std::string dst = def->Find("blendDst") ? def->Find("blendDst")->StringOr("") : "";
                if (!src.empty() || !dst.empty()) {
                    if (dst == "One") {
                        info.blend = MaterialInfo::Blend::Additive;
                        info.depthWrite = false;
                    } else if (dst == "OneMinusSrcAlpha" || dst == "OneMinusSourceAlpha") {
                        info.blend = MaterialInfo::Blend::Alpha;
                    }
                }
                out[name] = info;
            }
            pending.swap(next);
        }
    }

    void MaterialLibrary::Load(const std::string& root) {
        custom.clear();
        std::error_code ec;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(fs::path(root) / "materials", ec))
            if (e.is_regular_file() && e.path().extension() == ".material")
                files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& p : files) {
            std::ifstream f(p, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            const std::unordered_map<std::string, MaterialInfo> known = custom;
            ApplyJson(ss.str(), custom, known);
        }
    }

    MaterialInfo MaterialLibrary::Find(const std::string& name) const {
        const std::string key = Lower(name);
        if (const auto it = custom.find(key); it != custom.end())
            return it->second;
        return Builtin(key);
    }
}

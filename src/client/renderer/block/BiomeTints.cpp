#include "client/renderer/block/BiomeTints.hpp"
#include "client/resources/TextureLoader.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <filesystem>

namespace perlin {
    namespace {

        std::string ReadFile(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            if (!f)
                return {};
            return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }

        bool ParseHex(const std::string& hex, std::array<float, 3>& out) {
            if (hex.size() != 7 || hex[0] != '#')
                return false;
            unsigned r = 0, g = 0, b = 0;
            if (sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) != 3)
                return false;
            out = {(float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f};
            return true;
        }

        const nlohmann::json* Child(const nlohmann::json& j, const char* key) {
            if (!j.is_object())
                return nullptr;
            auto it = j.find(key);
            return it == j.end() ? nullptr : &*it;
        }

        bool ResolveTint(const nlohmann::json& value,
                         const std::unordered_map<std::string, std::array<float, 3>>& mapAvg,
                         std::array<float, 3>& out) {
            if (value.is_string()) {
                return ParseHex(value.get<std::string>(), out);
            }
            if (value.is_object()) {
                auto cm = value.find("color_map");
                if (cm != value.end() && cm->is_string()) {
                    auto it = mapAvg.find(cm->get<std::string>());
                    if (it != mapAvg.end()) {
                        out = it->second;
                        return true;
                    }
                    return false;
                }
            }
            return false;
        }

    }

    bool BiomeTints::EnsureLoaded(const std::string& vanillaRpRoot) {
        if (ready.load(std::memory_order_acquire))
            return true;
        attempts++;
        mapAvg.clear();
        mapRGB.clear();
        rules.clear();
        climate.clear();
        idToName.clear();
        nameToId.clear();

        static const char* kMaps[] = {"grass",     "foliage",     "dry_foliage",   "birch",
                                      "evergreen", "swamp_grass", "swamp_foliage", "mangrove_swamp_foliage"};
        bool anyMap = false;
        for (const char* m : kMaps) {
            std::string full = vanillaRpRoot + "\\textures\\colormap\\" + m + ".png";
            Texture tex;
            if (!LoadTexture(full, tex) || tex.width <= 0 || tex.height <= 0) {
                std::string tga = vanillaRpRoot + "\\textures\\colormap\\" + m + ".tga";
                if (!LoadTexture(tga, tex) || tex.width <= 0 || tex.height <= 0)
                    continue;
            }
            double sr = 0, sg = 0, sb = 0;
            int n = 0;
            for (size_t i = 0; i < tex.pixels.size(); i += 4) {
                int r = tex.pixels[i], g = tex.pixels[i + 1], b = tex.pixels[i + 2];
                if (r > 240 && g > 240 && b > 240)
                    continue;
                sr += r;
                sg += g;
                sb += b;
                n++;
            }
            if (n > 0) {
                mapAvg[m] = {(float)(sr / n / 255.0), (float)(sg / n / 255.0), (float)(sb / n / 255.0)};
                anyMap = true;
            }
            bool wantFull = !strcmp(m, "grass") || !strcmp(m, "foliage") || !strcmp(m, "dry_foliage");
            if (wantFull) {
                std::vector<uint8_t> rgb((size_t)256 * 256 * 3, 255);
                for (int yy = 0; yy < 256; yy++)
                    for (int xx = 0; xx < 256; xx++) {
                        int sx = xx * tex.width / 256, sy = yy * tex.height / 256;
                        const uint8_t* s = tex.pixels.data() + ((size_t)sy * tex.width + sx) * 4;
                        uint8_t* d = rgb.data() + ((size_t)yy * 256 + xx) * 3;
                        d[0] = s[0];
                        d[1] = s[1];
                        d[2] = s[2];
                    }
                mapRGB[m] = std::move(rgb);
            }
        }
        if (!anyMap)
            return false;

        {
            std::string defs =
                ReadFile(vanillaRpRoot + "\\..\\repo\\metadata\\vanilladata_modules\\mojang-biomes.json");
            try {
                auto j = nlohmann::json::parse(defs);
                if (j.contains("data_items") && j["data_items"].is_array()) {
                    for (auto& e : j["data_items"]) {
                        if (!e.contains("id") || !e.contains("name"))
                            continue;
                        int id = e["id"].get<int>();
                        std::string name = e["name"].get<std::string>();
                        idToName[id] = name;
                        nameToId[name] = id;
                    }
                }
            } catch (...) {}
        }

        {
            std::error_code dirEc;
            for (const auto& dirEntry : std::filesystem::directory_iterator(
                     vanillaRpRoot + "/../repo/behavior_pack/biomes", dirEc)) {
                if (!dirEntry.path().filename().string().ends_with(".biome.json"))
                    continue;
                {
                    std::string full = dirEntry.path().string();
                    std::string txt = ReadFile(full);
                    if (txt.empty())
                        continue;
                    try {
                        auto root = nlohmann::json::parse(txt);
                        const auto* biome = Child(root, "minecraft:biome");
                        const auto* desc = biome ? Child(*biome, "description") : nullptr;
                        const auto* ident = desc ? Child(*desc, "identifier") : nullptr;
                        const auto* comps = biome ? Child(*biome, "components") : nullptr;
                        const auto* cl = comps ? Child(*comps, "minecraft:climate") : nullptr;
                        if (!ident || !ident->is_string() || !cl || !cl->is_object())
                            continue;
                        const auto* tt = Child(*cl, "temperature");
                        const auto* dd = Child(*cl, "downfall");
                        if (!tt || !dd || !tt->is_number() || !dd->is_number())
                            continue;
                        climate[ident->get<std::string>()] = {(float)tt->get<double>(),
                                                              (float)dd->get<double>()};
                    } catch (...) {}
                }
            }
        }
        {
            std::error_code dirEc;
            for (const auto& dirEntry :
                 std::filesystem::directory_iterator(vanillaRpRoot + "/biomes", dirEc)) {
                if (!dirEntry.path().filename().string().ends_with(".client_biome.json"))
                    continue;
                {
                    std::string full = dirEntry.path().string();
                    std::string txt = ReadFile(full);
                    if (txt.empty())
                        continue;
                    try {
                        auto root = nlohmann::json::parse(txt);
                        const auto* biome = Child(root, "minecraft:client_biome");
                        const auto* desc = biome ? Child(*biome, "description") : nullptr;
                        const auto* ident = desc ? Child(*desc, "identifier") : nullptr;
                        if (!ident || !ident->is_string())
                            continue;
                        std::string name = ident->get<std::string>();
                        const auto* comp = Child(*biome, "components");
                        if (!comp)
                            continue;
                        BiomeColorRule& rule = rules[name];
                        std::array<float, 3> c;
                        auto colorOf = [&](const char* comp1, const char* key) -> const nlohmann::json* {
                            const auto* a = Child(*comp, comp1);
                            return a ? Child(*a, key) : nullptr;
                        };
                        if (auto* v = colorOf("minecraft:grass_appearance", "color");
                            v && ResolveTint(*v, mapAvg, c)) {
                            rule.grass = c;
                            rule.hasGrass = true;
                        }
                        if (auto* v = colorOf("minecraft:foliage_appearance", "color");
                            v && ResolveTint(*v, mapAvg, c)) {
                            rule.foliage = c;
                            rule.hasFoliage = true;
                        }
                        if (auto* v = colorOf("minecraft:dry_foliage_color", "color");
                            v && ResolveTint(*v, mapAvg, c)) {
                            rule.dryFoliage = c;
                            rule.hasDry = true;
                        }
                        if (auto* v = colorOf("minecraft:water_appearance", "surface_color");
                            v && v->is_string() && ParseHex(v->get<std::string>(), c)) {
                            rule.water = c;
                            rule.hasWater = true;
                        }
                    } catch (...) {}
                }
            }
        }

        {
            std::string txt = ReadFile(vanillaRpRoot + "/biomes_client.json");
            std::string noComments;
            noComments.reserve(txt.size());
            for (size_t i = 0; i < txt.size(); i++) {
                if (txt[i] == '/' && i + 1 < txt.size() && txt[i + 1] == '/') {
                    while (i < txt.size() && txt[i] != '\n')
                        i++;
                } else {
                    noComments.push_back(txt[i]);
                }
            }
            try {
                auto b = nlohmann::json::parse(noComments);
                if (b.contains("biomes") && b["biomes"].is_object()) {
                    const nlohmann::json* pick = nullptr;
                    if (b["biomes"].contains("default"))
                        pick = &b["biomes"]["default"];
                    else if (b["biomes"].contains("minecraft:plains"))
                        pick = &b["biomes"]["minecraft:plains"];
                    if (pick && pick->contains("water_surface_color") &&
                        (*pick)["water_surface_color"].is_string()) {
                        std::array<float, 3> wc;
                        if (ParseHex((*pick)["water_surface_color"].get<std::string>(), wc))
                            defaultWater = wc;
                    }
                }
            } catch (...) {}
        }

        auto avg = [&](const char* m, std::array<float, 3> fb) {
            auto it = mapAvg.find(m);
            return it == mapAvg.end() ? fb : it->second;
        };
        std::array<float, 3> plainsG = Grass("minecraft:plains");
        std::array<float, 3> plainsF = Foliage("minecraft:plains");
        std::cout << "[biomes] colormaps=" << mapAvg.size() << " rules=" << rules.size()
                  << " climates=" << climate.size() << " plainsGrass=(" << plainsG[0] << "," << plainsG[1]
                  << "," << plainsG[2] << ") plainsFoliage=(" << plainsF[0] << "," << plainsF[1] << ","
                  << plainsF[2] << ")" << " grassAvg=(" << avg("grass", {0, 0, 0})[0] << ","
                  << avg("grass", {0, 0, 0})[1] << "," << avg("grass", {0, 0, 0})[2] << ")" << " water=("
                  << defaultWater[0] << "," << defaultWater[1] << "," << defaultWater[2] << ")" << std::endl;

        bool complete = mapRGB.count("grass") && mapRGB.count("foliage") && !climate.empty() &&
                        !rules.empty() && !idToName.empty();
        if (!complete && attempts < 10)
            return false;

        ready.store(true, std::memory_order_release);
        return true;
    }

    std::array<float, 3> BiomeTints::MapAverage(const std::string& mapName) const {
        auto it = mapAvg.find(mapName);
        if (it != mapAvg.end())
            return it->second;
        return {1, 1, 1};
    }

    std::array<float, 3> BiomeTints::SampleMap(const std::vector<uint8_t>& rgb, float temperature,
                                               float downfall) const {
        float t = Clamp01(temperature);
        float d = Clamp01(downfall);
        int x = (int)((1.0f - t) * 255.0f);
        int y = (int)((1.0f - t * d) * 255.0f);
        if (x < 0)
            x = 0;
        if (x > 255)
            x = 255;
        if (y < 0)
            y = 0;
        if (y > 255)
            y = 255;
        const uint8_t* p = rgb.data() + ((size_t)y * 256 + x) * 3;
        return {(float)p[0] / 255.0f, (float)p[1] / 255.0f, (float)p[2] / 255.0f};
    }

    std::array<float, 3> BiomeTints::SampleMapChecked(const std::vector<uint8_t>& rgb, float t, float d,
                                                      const char* mapName) const {
        auto c = SampleMap(rgb, t, d);
        if (c[0] > 0.94f && c[1] > 0.94f && c[2] > 0.94f) {
            auto it = mapAvg.find(mapName);
            if (it != mapAvg.end())
                return it->second;
        }
        return c;
    }

    std::array<float, 3> BiomeTints::Grass(const std::string& biomeName) const {
        auto it = rules.find(biomeName);
        if (it != rules.end() && it->second.hasGrass)
            return it->second.grass;
        auto cl = climate.find(biomeName);
        auto gm = mapRGB.find("grass");
        if (cl != climate.end() && gm != mapRGB.end())
            return SampleMapChecked(gm->second, cl->second[0], cl->second[1], "grass");
        return {0.4745f, 0.7529f, 0.3529f};
    }

    std::array<float, 3> BiomeTints::Foliage(const std::string& biomeName) const {
        auto it = rules.find(biomeName);
        if (it != rules.end() && it->second.hasFoliage)
            return it->second.foliage;
        auto cl = climate.find(biomeName);
        auto fm = mapRGB.find("foliage");
        if (cl != climate.end() && fm != mapRGB.end())
            return SampleMapChecked(fm->second, cl->second[0], cl->second[1], "foliage");
        return {0.42f, 0.65f, 0.28f};
    }

    std::array<float, 3> BiomeTints::DryFoliage(const std::string& biomeName) const {
        auto it = rules.find(biomeName);
        if (it != rules.end() && it->second.hasDry)
            return it->second.dryFoliage;
        auto g = mapAvg.find("dry_foliage");
        if (g != mapAvg.end())
            return g->second;
        return Foliage(biomeName);
    }

    std::array<float, 3> BiomeTints::Water(const std::string& biomeName) const {
        auto it = rules.find(biomeName);
        if (it != rules.end() && it->second.hasWater)
            return it->second.water;
        return defaultWater;
    }

    std::string BiomeTints::NameForId(int id) const {
        auto it = idToName.find(id);
        if (it != idToName.end())
            return it->second;
        return "minecraft:plains";
    }

    int BiomeTints::IdForName(const std::string& name) const {
        auto it = nameToId.find(name);
        if (it != nameToId.end())
            return it->second;
        return 1;
    }

}

#include "client/renderer/block/BlockLibrary.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "client/resources/TextureLoader.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include "util/Paths.hpp"
#include "client/Options.hpp"

namespace perlin {
    namespace {

        std::string ReadFile(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            if (!f)
                return {};
            return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }

        void WriteFile(const std::string& path, const char* content) {
            if (FILE* f = fopen(path.c_str(), "w")) {
                fwrite(content, 1, strlen(content), f);
                fclose(f);
            }
        }

        bool SeedFromExeDir(const std::string& fileName, const std::string& destPath) {
            std::string src = paths::ExeDir() + "/resources/" + fileName;
            std::string content = ReadFile(src);
            if (content.empty())
                return false;
            WriteFile(destPath, content.c_str());
            return true;
        }

    }

    std::string BlockLibrary::StripComments(const std::string& json) {
        std::string out;
        out.reserve(json.size());
        for (size_t i = 0; i < json.size(); i++) {
            if (json[i] == '/' && i + 1 < json.size() && json[i + 1] == '/') {
                while (i < json.size() && json[i] != '\n')
                    i++;
            } else {
                out.push_back(json[i]);
            }
        }
        return out;
    }

    int BlockLibrary::FetchTile(const std::string& pngRelPath, const std::string& vanillaRpRoot) {
        auto it = tileByPng.find(pngRelPath);
        if (it != tileByPng.end())
            return it->second;
        if (nextTile >= kAtlasTiles * kAtlasTiles)
            return 0;
        std::string full = vanillaRpRoot + "/" + pngRelPath + ".png";
        int w = 0, h = 0;
        Texture tex;
        std::vector<std::string> tries;
        for (auto& base : {pngRelPath, pngRelPath + "_opaque"}) {
            tries.push_back(base + ".png");
            tries.push_back(base + ".tga");
        }
        int tile = 0;
        bool ok = false;
        for (auto& t : tries) {
            full = vanillaRpRoot + "/" + t;
            if (LoadTexture(full, tex) && tex.width > 0 && tex.height > 0) {
                w = tex.width;
                h = tex.height;
                ok = true;
                break;
            }
        }
        if (!ok) {
            fetchFails++;
            return 0;
        }
        const unsigned char* data = tex.pixels.data();
        bool framed = (w % kTilePx == 0) && (h % kTilePx == 0);
        tile = nextTile++;
        tileByPng[pngRelPath] = tile;
        TileAlpha alpha;
        double satSum = 0;
        int satN = 0;
        int frameOpaque = 0;
        int opaqueAll = 0;
        int partialAlpha = 0;
        size_t atlasW = (size_t)kAtlasTiles * kTilePx;
        size_t base =
            ((size_t)(tile / kAtlasTiles) * kTilePx) * atlasW + (size_t)(tile % kAtlasTiles) * kTilePx;
        for (int y = 0; y < kTilePx; y++) {
            for (int x = 0; x < kTilePx; x++) {
                int sx, sy;
                if (framed) {
                    sx = x;
                    sy = y;
                } else if (w == kTilePx && h > kTilePx) {
                    sx = x;
                    sy = y;
                } else {
                    sx = (x * w / kTilePx);
                    sy = (y * h / kTilePx);
                }
                if (sx < 0)
                    sx = 0;
                if (sx >= w)
                    sx = w - 1;
                if (sy < 0)
                    sy = 0;
                if (sy >= h)
                    sy = h - 1;
                const unsigned char* src = data + ((size_t)sy * w + sx) * 4;
                uint8_t* d = atlas.data() + (base + (size_t)y * atlasW + x) * 4;
                d[0] = src[0];
                d[1] = src[1];
                d[2] = src[2];
                d[3] = src[3];
                if (src[3] > 20 && src[3] < 235)
                    partialAlpha++;
                if (src[3] < 128) {
                    alpha.holes = true;
                    continue;
                }
                opaqueAll++;
                if (src[0] | src[1] | src[2]) {
                    int mx = src[0] > src[1] ? (src[0] > src[2] ? src[0] : src[2])
                                             : (src[1] > src[2] ? src[1] : src[2]);
                    int mn = src[0] < src[1] ? (src[0] < src[2] ? src[0] : src[2])
                                             : (src[1] < src[2] ? src[1] : src[2]);
                    satSum += (double)(mx - mn) / mx;
                    satN++;
                    if (x == 0 || x == kTilePx - 1 || y == 0 || y == kTilePx - 1)
                        frameOpaque++;
                } else if (x == 0 || x == kTilePx - 1 || y == 0 || y == kTilePx - 1) {
                    frameOpaque++;
                }
            }
        }
        {
            int cx = w / 2, cy = h / 2;
            if (framed) {
                cx = kTilePx / 2;
                cy = kTilePx / 2;
            }
            if (cx < 0)
                cx = 0;
            if (cx >= w)
                cx = w - 1;
            if (cy < 0)
                cy = 0;
            if (cy >= h)
                cy = h - 1;
            alpha.centerHollow = data[((size_t)cy * w + cx) * 4 + 3] < 128;
        }
        alpha.translucent = partialAlpha >= kTilePx * kTilePx / 2;
        alphaByTile[tile] = alpha;
        if (alphaByTile.find(tile) != alphaByTile.end() && satN > 0)
            alphaByTile[tile].saturation = (float)(satSum / satN);
        alphaByTile[tile].frame = (float)frameOpaque / (float)(4 * (kTilePx - 1));
        alphaByTile[tile].holeFrac = 1.0f - (float)opaqueAll / (float)(kTilePx * kTilePx);
        pathByTile[tile] = pngRelPath;
        if (framed && (w > kTilePx || h > kTilePx) && h >= kTilePx)
            animByTile[tile] = AnimSrc{tex.pixels, w, h};
        return tile;
    }

    BlockLibrary::TileAlpha BlockLibrary::AlphaOf(int tile) const {
        if (tile <= 0)
            return {};
        auto it = alphaByTile.find(tile);
        return it == alphaByTile.end() ? TileAlpha{} : it->second;
    }

    float BlockLibrary::SaturationOf(int tile) const {
        if (tile <= 0)
            return 0;
        auto it = alphaByTile.find(tile);
        return it == alphaByTile.end() ? 0 : it->second.saturation;
    }

    std::string BlockLibrary::TilePathOf(int tile) const {
        auto it = pathByTile.find(tile);
        return it == pathByTile.end() ? std::string() : it->second;
    }

    void BlockLibrary::PollAnimation(uint64_t step,
                                     const std::function<void(int tile, const uint8_t* rgba16)>& emit) {
        if (!ready || animByTile.empty())
            return;
        for (auto& [tile, src] : animByTile) {
            int frames = src.h / kTilePx;
            if (frames <= 1)
                continue;
            int f = (int)(step % (uint64_t)frames);
            for (int y = 0; y < kTilePx; y++) {
                const uint8_t* s = src.pixels.data() + ((size_t)(f * kTilePx + y) * src.w) * 4;
                uint8_t* d = animScratch.data() + (size_t)y * kTilePx * 4;
                memcpy(d, s, (size_t)kTilePx * 4);
            }
            emit(tile, animScratch.data());
        }
    }

    bool BlockLibrary::EnsureLoaded(const std::string& perlinPackDir, const std::string& vanillaRpRoot) {
        if (ready && complete)
            return true;
        fetchFails = 0;

        std::string vanillaBlocks = ReadFile(vanillaRpRoot + "/blocks.json");
        std::string terrainTex = ReadFile(vanillaRpRoot + "/textures/terrain_texture.json");
        if (vanillaBlocks.empty() || terrainTex.empty())
            return false;

        nlohmann::json vBlocks;
        nlohmann::json vTerrain;
        try {
            vBlocks = nlohmann::json::parse(vanillaBlocks);
            vTerrain = nlohmann::json::parse(StripComments(terrainTex));
        } catch (...) {
            return false;
        }
        const nlohmann::json& texData =
            vTerrain.contains("texture_data") ? vTerrain["texture_data"] : vTerrain;

        if (!BlockPalette::Instance().NamesLoaded()) {
            std::string defs = vanillaRpRoot + "/../repo/metadata/vanilladata_modules/mojang-blocks.json";
            if (!BlockPalette::Instance().LoadVanillaDefinitions(defs))
                return false;
        }
        const std::vector<std::string> allNames = BlockPalette::Instance().AllNames();
        int noTexture = 0;

        std::string blocksPath = perlinPackDir + "/blocks.json";
        if (ReadFile(blocksPath).empty())
            SeedFromExeDir("blocks.json", blocksPath);
        nlohmann::json blocksJ = nlohmann::json::object();
        try {
            std::string txt = ReadFile(blocksPath);
            if (!txt.empty())
                blocksJ = nlohmann::json::parse(txt);
        } catch (...) {
            std::cout << "[blocks] ignoring unreadable " << blocksPath << std::endl;
        }

        std::unordered_map<std::string, std::string> aliases;
        std::unordered_map<std::string, std::array<float, 3>> tints;
        if (blocksJ.contains("aliases") && blocksJ["aliases"].is_object())
            for (auto& [k, v] : blocksJ["aliases"].items())
                aliases[k] = v.get<std::string>();
        if (blocksJ.contains("tints") && blocksJ["tints"].is_object())
            for (auto& [k, v] : blocksJ["tints"].items())
                if (v.is_array() && v.size() >= 3)
                    tints[k] = {(float)v[0], (float)v[1], (float)v[2]};
        const nlohmann::json& overrides =
            (blocksJ.contains("blocks") && blocksJ["blocks"].is_object()) ? blocksJ["blocks"] : blocksJ;

        std::vector<std::string> flatContains;
        {
            std::string shapesPath = perlinPackDir + "/shapes.json";
            if (ReadFile(shapesPath).empty())
                SeedFromExeDir("shapes.json", shapesPath);
            try {
                std::string txt = ReadFile(shapesPath);
                if (!txt.empty()) {
                    auto sj = nlohmann::json::parse(txt);
                    if (sj.contains("flat_contains") && sj["flat_contains"].is_array())
                        for (auto& e : sj["flat_contains"])
                            if (e.is_string())
                                flatContains.push_back(e.get<std::string>());
                }
            } catch (...) {
                std::cout << "[blocks] ignoring unreadable " << shapesPath << std::endl;
            }
        }

        size_t atlasPx = (size_t)kAtlasTiles * kTilePx;
        atlas.assign(atlasPx * atlasPx * 4, 0);
        for (int i = 0; i < kTilePx * kTilePx; i++) {
            atlas[(size_t)i * 4 + 0] = 255;
            atlas[(size_t)i * 4 + 1] = 255;
            atlas[(size_t)i * 4 + 2] = 255;
            atlas[(size_t)i * 4 + 3] = 255;
        }

        auto resolveTexture = [&](const std::string& texName, std::vector<int>* all = nullptr) -> int {
            if (texName.rfind("path:", 0) == 0) {
                int t = FetchTile(texName.substr(5), vanillaRpRoot);
                if (all && t)
                    all->push_back(t);
                return t;
            }
            auto it = texData.find(texName);
            if (it == texData.end()) {
                int t = FetchTile(texName, vanillaRpRoot);
                if (t == 0 && texName.find('/') == std::string::npos)
                    t = FetchTile(std::string("textures/blocks/") + texName, vanillaRpRoot);
                if (all && t)
                    all->push_back(t);
                return t;
            }
            const nlohmann::json& entry = *it;
            if (entry.is_object()) {
                auto oc = entry.find("tint_color");
                if (oc != entry.end() && oc->is_string()) {
                    const std::string& hex = oc->get_ref<const std::string&>();
                    unsigned r = 255, g = 255, b = 255;
                    if (hex.size() == 7 && hex[0] == '#' &&
                        sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3)
                        baseTints[texName] = {(float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f};
                }
            }
            if (entry.is_string()) {
                int t = FetchTile(entry.get<std::string>(), vanillaRpRoot);
                if (all && t)
                    all->push_back(t);
                return t;
            }
            const nlohmann::json* list = nullptr;
            if (entry.is_object() && entry.contains("textures")) {
                const nlohmann::json& tx = entry["textures"];
                if (tx.is_string()) {
                    int t = FetchTile(tx.get<std::string>(), vanillaRpRoot);
                    if (all && t)
                        all->push_back(t);
                    return t;
                }
                list = &tx;
            } else if (entry.is_array()) {
                list = &entry;
            }
            if (!list || list->empty())
                return 0;
            int first = 0;
            for (const nlohmann::json& e : *list) {
                std::string path;
                if (e.is_string())
                    path = e.get<std::string>();
                else if (e.is_object() && e.contains("path") && e["path"].is_string())
                    path = e["path"].get<std::string>();
                if (path.empty())
                    continue;
                int tile = FetchTile(path, vanillaRpRoot);
                if (tile != 0) {
                    if (!first)
                        first = tile;
                    if (all)
                        all->push_back(tile);
                }
            }
            return first;
        };

        appearances["minecraft:air"] = BlockAppearance{};

        for (const std::string& name : allNames) {
            if (name == "minecraft:air")
                continue;
            try {
                std::string bare = name;
                if (bare.rfind("minecraft:", 0) == 0)
                    bare = bare.substr(10);
                auto al = aliases.find(bare);
                std::string vkey = (al == aliases.end()) ? bare : al->second;
                if (vBlocks.find(vkey) == vBlocks.end()) {
                    std::string rpKey = BlockPalette::Instance().ResourcePackKey(name);
                    if (!rpKey.empty() && vBlocks.find(rpKey) != vBlocks.end())
                        vkey = rpKey;
                }

                std::string top, side, bottom;
                auto getFace = [&](const nlohmann::json& e, const char* face, std::string& out) {
                    if (e.is_string()) {
                        out = e.get<std::string>();
                        return;
                    }
                    if (e.is_object()) {
                        auto f = e.find(face);
                        if (f != e.end() && f->is_string()) {
                            out = f->get<std::string>();
                            return;
                        }
                        if (!strcmp(face, "side")) {
                            static const char* kSideKeys[] = {"side", "east", "west", "north", "south"};
                            for (auto* sk : kSideKeys) {
                                auto s = e.find(sk);
                                if (s != e.end() && s->is_string()) {
                                    out = s->get<std::string>();
                                    return;
                                }
                            }
                        }
                        if (!strcmp(face, "up") || !strcmp(face, "top")) {
                            auto t = e.find("top");
                            if (t != e.end() && t->is_string()) {
                                out = t->get<std::string>();
                                return;
                            }
                        }
                    }
                };
                auto vit = vBlocks.find(vkey);
                bool hasCarried = false, hasIsotropic = false, hasAO = false;
                if (vit != vBlocks.end() && vit->is_object() && vit->contains("textures")) {
                    const nlohmann::json& t = (*vit)["textures"];
                    getFace(t, "up", top);
                    getFace(t, "down", bottom);
                    getFace(t, "side", side);
                    if (side.empty())
                        side = top;
                    if (bottom.empty())
                        bottom = top;
                    if (top.empty())
                        top = side;
                }
                if (vit != vBlocks.end() && vit->is_object()) {
                    hasCarried = vit->contains("carried_textures");
                    hasIsotropic = vit->contains("isotropic");
                    hasAO = vit->contains("ambient_occlusion_exponent");
                }

                BlockAppearance app;
                bool hasOverride = overrides.contains(name) && overrides[name].is_object();
                const nlohmann::json& ov = hasOverride ? overrides[name] : nlohmann::json();
                auto pick = [&](const char* key, const std::string& vanilla) -> std::string {
                    if (hasOverride && ov.contains(key) && ov[key].is_string())
                        return ov[key].get<std::string>();
                    return vanilla;
                };
                std::string allOv = (hasOverride && ov.contains("texture") && ov["texture"].is_string())
                                        ? ov["texture"].get<std::string>()
                                        : std::string();
                std::string topName = allOv.empty() ? pick("top", top) : allOv;
                std::string sideName = allOv.empty() ? pick("side", side.empty() ? top : side) : allOv;
                std::string botName = allOv.empty() ? pick("bottom", bottom.empty() ? top : bottom) : allOv;
                if (topName.empty() && sideName.empty() && botName.empty()) {
                    noTexture++;
                    continue;
                }
                std::vector<int> varTop, varSide, varBottom;
                app.top = topName.empty() ? 0 : resolveTexture(topName, &varTop);
                app.side = sideName.empty() ? app.top : resolveTexture(sideName, &varSide);
                if (sideName.empty())
                    varSide = varTop;
                app.bottom = botName.empty() ? app.top : resolveTexture(botName, &varBottom);
                if (botName.empty())
                    varBottom = varTop;
                auto collapseModes = [&](std::vector<int>& v) {
                    if (v.size() <= 1)
                        return;
                    auto stemOf = [&](int tile) {
                        auto it = pathByTile.find(tile);
                        std::string p = it == pathByTile.end() ? std::string() : it->second;
                        auto dot = p.find_last_of('.');
                        if (dot != std::string::npos)
                            p.resize(dot);
                        const std::string op = "_opaque";
                        if (p.size() > op.size() && p.compare(p.size() - op.size(), op.size(), op) == 0)
                            p.resize(p.size() - op.size());
                        return p;
                    };
                    std::string s0 = stemOf(v[0]);
                    for (size_t i = 1; i < v.size(); i++) {
                        if (stemOf(v[i]) != s0)
                            return;
                    }
                    v.resize(1);
                };
                collapseModes(varTop);
                collapseModes(varSide);
                collapseModes(varBottom);
                for (auto* vp : {&varTop, &varSide, &varBottom}) {
                    std::vector<int> u;
                    for (int t : *vp) {
                        if (u.empty() || u.back() != t)
                            u.push_back(t);
                    }
                    *vp = std::move(u);
                }
                app.varTop = varTop;
                app.varSide = varSide;
                app.varBottom = varBottom;
                app.texTop = topName;
                app.texSide = sideName;
                app.texBottom = botName;
                if (hasOverride && ov.contains("tint_faces") && ov["tint_faces"].is_array()) {
                    auto has = [&](const char* f) {
                        for (auto& e : ov["tint_faces"])
                            if (e.is_string() && e.get<std::string>() == f)
                                return true;
                        return false;
                    };
                    auto tintFor = [&](const std::string& tex) -> std::array<float, 3> {
                        if (hasOverride && ov.contains("tint") && ov["tint"].is_array() &&
                            ov["tint"].size() >= 3)
                            return {(float)ov["tint"][0], (float)ov["tint"][1], (float)ov["tint"][2]};
                        auto oo = baseTints.find(tex);
                        if (oo != baseTints.end())
                            return oo->second;
                        auto it = tints.find(tex);
                        if (it != tints.end())
                            return it->second;
                        return {1, 1, 1};
                    };
                    if (has("top") || has("side") || has("bottom")) {
                        auto tc = tintFor(topName);
                        app.tintR = tc[0];
                        app.tintG = tc[1];
                        app.tintB = tc[2];
                        app.tintTop = has("top");
                        app.tintSide = has("side");
                        app.tintBottom = has("bottom");
                    }
                    if (ov.contains("cutout") && ov["cutout"].is_boolean())
                        app.cutout = ov["cutout"].get<bool>();
                } else {
                    auto findTint = [&](const std::string& tex) -> const std::array<float, 3>* {
                        auto oo = baseTints.find(tex);
                        if (oo != baseTints.end())
                            return &oo->second;
                        auto tt = tints.find(tex);
                        if (tt != tints.end())
                            return &tt->second;
                        return nullptr;
                    };
                    const std::array<float, 3>* tcTop = findTint(topName);
                    const std::array<float, 3>* tcSide = findTint(sideName);
                    const std::array<float, 3>* tcBottom = findTint(botName);
                    if (tcTop || tcSide || tcBottom) {
                        const std::array<float, 3>* tc = tcTop ? tcTop : (tcSide ? tcSide : tcBottom);
                        app.tintR = (*tc)[0];
                        app.tintG = (*tc)[1];
                        app.tintB = (*tc)[2];
                        app.tintTop = tcTop != nullptr;
                        app.tintSide = tcSide != nullptr;
                        app.tintBottom = tcBottom != nullptr;
                    }
                }

                {
                    bool perFace = !(topName == sideName && sideName == botName);
                    bool flat = BlockPalette::Instance().BlockHasProperty(name, "height");
                    if (!flat && !perFace && hasCarried &&
                        BlockPalette::Instance().BlockHasProperty(name, "growth")) {
                        TileAlpha ga = AlphaOf(app.top);
                        if (ga.holes)
                            flat = true;
                    }
                    if (!flat) {
                        for (auto& sub : flatContains) {
                            if (sub.empty())
                                continue;
                            if (bare.find(sub) != std::string::npos) {
                                flat = true;
                                break;
                            }
                        }
                    }
                    bool hasUpper = BlockPalette::Instance().BlockHasProperty(name, "upper_block_bit");
                    bool hasHalf = BlockPalette::Instance().BlockHasProperty(name, "minecraft:vertical_half");
                    if (hasCarried && !hasIsotropic && !hasAO && !perFace && vit != vBlocks.end() &&
                        vit->is_object() && vit->contains("carried_textures")) {
                        const nlohmann::json& ct = (*vit)["carried_textures"];
                        if (ct.is_string()) {
                            app.carriedTex = ct.get<std::string>();
                            app.useCarriedColor = true;
                        } else if (ct.is_object()) {
                            auto s = ct.find("side");
                            if (s != ct.end() && s->is_string()) {
                                app.carriedTex = s->get<std::string>();
                                app.useCarriedColor = true;
                            }
                        }
                    }
                    bool hasTile = (app.top | app.side | app.bottom) != 0;
                    if (!hasTile) {
                        app.shape = BlockAppearance::Shape::Cube;
                    } else if (flat) {
                        app.shape = BlockAppearance::Shape::Flat;
                    } else if (hasHalf || hasUpper) {
                        app.shape = hasUpper ? BlockAppearance::Shape::Cross : BlockAppearance::Shape::Cube;
                    } else if (!perFace &&
                               (BlockPalette::Instance().BlockHasProperty(name, "facing_direction") ||
                                BlockPalette::Instance().BlockHasProperty(name, "vine_direction_bits"))) {
                        TileAlpha aw = AlphaOf(app.top);
                        app.shape = aw.holes ? BlockAppearance::Shape::Wall : BlockAppearance::Shape::Cube;
                    } else if (!perFace) {
                        TileAlpha a = AlphaOf(app.top);
                        if (!a.holes)
                            app.shape = BlockAppearance::Shape::Cube;
                        else if (hasCarried && (hasIsotropic || hasAO))
                            app.shape = BlockAppearance::Shape::Cube;
                        else if (a.frame >= 0.5f)
                            app.shape = BlockAppearance::Shape::Cube;
                        else
                            app.shape = BlockAppearance::Shape::Cross;
                    } else {
                        TileAlpha at = AlphaOf(app.top), as = AlphaOf(app.side), ab = AlphaOf(app.bottom);
                        bool anySolid =
                            (app.top && !at.holes) || (app.side && !as.holes) || (app.bottom && !ab.holes);
                        app.shape = anySolid ? BlockAppearance::Shape::Cube : BlockAppearance::Shape::Cross;
                    }
                    if (app.shape == BlockAppearance::Shape::Cross ||
                        app.shape == BlockAppearance::Shape::Wall)
                        app.cutout = true;
                    app.overlayFrac = 0;
                    if (app.top && app.side && app.top != app.side) {
                        TileAlpha atop = AlphaOf(app.top);
                        if (!atop.holes) {
                            size_t atlasW = (size_t)kAtlasTiles * kTilePx;
                            size_t sbase = ((size_t)(app.side / kAtlasTiles) * kTilePx) * atlasW +
                                           (size_t)(app.side % kAtlasTiles) * kTilePx;
                            int rows = 0;
                            for (int yy = 0; yy < kTilePx; yy++) {
                                int opq = 0;
                                double ss = 0;
                                for (int xx = 0; xx < kTilePx; xx++) {
                                    const uint8_t* d = atlas.data() + (sbase + (size_t)yy * atlasW + xx) * 4;
                                    if (d[3] < 128)
                                        continue;
                                    opq++;
                                    int mx = d[0] > d[1] ? (d[0] > d[2] ? d[0] : d[2])
                                                         : (d[1] > d[2] ? d[1] : d[2]);
                                    int mn = d[0] < d[1] ? (d[0] < d[2] ? d[0] : d[2])
                                                         : (d[1] < d[2] ? d[1] : d[2]);
                                    if (mx > 0)
                                        ss += (double)(mx - mn) / mx;
                                }
                                if (opq < kTilePx / 2)
                                    break;
                                if (opq > 0 && ss / opq > 0.3)
                                    break;
                                rows++;
                            }
                            if (rows > 0 && rows <= kTilePx / 2)
                                app.overlayFrac = (float)rows / (float)kTilePx;
                        }
                    }
                    if (app.overlayFrac > 0 && app.side && nextTile < kAtlasTiles * kAtlasTiles) {
                        const int overlaySrc = app.side;
                        size_t atlasW = (size_t)kAtlasTiles * kTilePx;
                        auto tileBase = [&](int t) {
                            return ((size_t)(t / kAtlasTiles) * kTilePx) * atlasW +
                                   (size_t)(t % kAtlasTiles) * kTilePx;
                        };
                        size_t sb = tileBase(overlaySrc);
                        bool grassPx[kTilePx][kTilePx] = {};
                        int kept = 0;
                        const int maxDepth = (kTilePx * 3) / 4;
                        for (int xx = 0; xx < kTilePx; xx++) {
                            for (int yy = 0; yy < maxDepth; yy++) {
                                const uint8_t* d = atlas.data() + (sb + (size_t)yy * atlasW + xx) * 4;
                                if (d[3] < 128)
                                    break;
                                int mx =
                                    d[0] > d[1] ? (d[0] > d[2] ? d[0] : d[2]) : (d[1] > d[2] ? d[1] : d[2]);
                                int mn =
                                    d[0] < d[1] ? (d[0] < d[2] ? d[0] : d[2]) : (d[1] < d[2] ? d[1] : d[2]);
                                double sat = mx > 0 ? (double)(mx - mn) / mx : 0.0;
                                if (sat > 0.3)
                                    break;
                                grassPx[yy][xx] = true;
                                kept++;
                            }
                        }
                        int deepest = 0;
                        for (int yy = 0; yy < kTilePx; yy++)
                            for (int xx = 0; xx < kTilePx; xx++)
                                if (grassPx[yy][xx])
                                    deepest = yy;
                        if (kept > 0) {
                            int mt = nextTile++;
                            size_t mb = tileBase(mt);
                            for (int yy = 0; yy < kTilePx; yy++)
                                for (int xx = 0; xx < kTilePx; xx++) {
                                    const uint8_t* d = atlas.data() + (sb + (size_t)yy * atlasW + xx) * 4;
                                    uint8_t* o = atlas.data() + (mb + (size_t)yy * atlasW + xx) * 4;
                                    if (grassPx[yy][xx]) {
                                        int lum = (int)(0.299 * d[0] + 0.587 * d[1] + 0.114 * d[2] + 0.5);
                                        o[0] = o[1] = o[2] = (uint8_t)lum;
                                        o[3] = 255;
                                    } else {
                                        o[0] = o[1] = o[2] = o[3] = 0;
                                    }
                                }
                            {
                                TileAlpha ta;
                                ta.holes = true;
                                alphaByTile[mt] = ta;
                                pathByTile[mt] = std::string("__grass_overlay_") + std::to_string(overlaySrc);
                            }
                            app.overlayTile = mt;

                            auto makeBase = [&](int dirtTile) -> int {
                                if (nextTile >= kAtlasTiles * kAtlasTiles)
                                    return overlaySrc;
                                int ct = nextTile++;
                                size_t cb = tileBase(ct);
                                size_t db = dirtTile ? tileBase(dirtTile) : 0;
                                for (int yy = 0; yy < kTilePx; yy++)
                                    for (int xx = 0; xx < kTilePx; xx++) {
                                        const uint8_t* sp =
                                            atlas.data() + (sb + (size_t)yy * atlasW + xx) * 4;
                                        uint8_t* o = atlas.data() + (cb + (size_t)yy * atlasW + xx) * 4;
                                        if (grassPx[yy][xx]) {
                                            o[0] = o[1] = o[2] = 0;
                                            o[3] = 0;
                                            continue;
                                        }
                                        if (dirtTile) {
                                            const uint8_t* dp =
                                                atlas.data() + (db + (size_t)yy * atlasW + xx) * 4;
                                            int a = sp[3];
                                            for (int c = 0; c < 3; c++)
                                                o[c] = (uint8_t)((sp[c] * a + dp[c] * (255 - a) + 127) / 255);
                                            o[3] = 255;
                                        } else {
                                            o[0] = sp[0];
                                            o[1] = sp[1];
                                            o[2] = sp[2];
                                            o[3] = sp[3];
                                        }
                                        if (yy <= deepest && o[3] == 255) {
                                            int mxc = o[0] > o[1] ? (o[0] > o[2] ? o[0] : o[2])
                                                                  : (o[1] > o[2] ? o[1] : o[2]);
                                            int mnc = o[0] < o[1] ? (o[0] < o[2] ? o[0] : o[2])
                                                                  : (o[1] < o[2] ? o[1] : o[2]);
                                            bool pebble = mxc > 0 && (double)(mxc - mnc) / mxc < 0.2;
                                            if (!pebble) {
                                                static const int cap[3] = {80, 55, 36};
                                                for (int c = 0; c < 3; c++)
                                                    if (o[c] > cap[c])
                                                        o[c] = (uint8_t)cap[c];
                                            }
                                        }
                                    }
                                TileAlpha ta;
                                ta.holes = true;
                                ta.holeFrac = (float)kept / (float)(kTilePx * kTilePx);
                                alphaByTile[ct] = ta;
                                pathByTile[ct] = std::string("__grass_base_") + std::to_string(dirtTile);
                                return ct;
                            };
                            TileAlpha as = AlphaOf(overlaySrc);
                            bool hollow = as.holeFrac > 0.5f && app.bottom && app.side != app.bottom;
                            if (hollow) {
                                std::vector<int> newVar;
                                for (int dt : app.varBottom)
                                    newVar.push_back(makeBase(dt));
                                app.side = makeBase(app.bottom);
                                app.varSide = newVar;
                            } else {
                                app.side = makeBase(0);
                                app.varSide.clear();
                            }
                        } else {
                            app.overlayFrac = 0;
                        }
                    }
                    if (hasOverride && ov.contains("cutout") && ov["cutout"].is_boolean()) {
                        if (ov["cutout"].get<bool>()) {
                            if (app.shape != BlockAppearance::Shape::Flat &&
                                app.shape != BlockAppearance::Shape::Wall)
                                app.shape = BlockAppearance::Shape::Cross;
                            app.cutout = true;
                        } else {
                            app.cutout = false;
                            if (app.shape == BlockAppearance::Shape::Cross ||
                                app.shape == BlockAppearance::Shape::Wall)
                                app.shape = BlockAppearance::Shape::Cube;
                        }
                    }
                }
                appearances[name] = app;
            } catch (const std::exception& e) {
                printf("[blocks] bad entry for %s: %s\n", name.c_str(), e.what());
            } catch (...) {
                printf("[blocks] bad entry for %s\n", name.c_str());
            }
        }

        if (fetchFails > 0) {
            if (fetchFails != lastFileMisses) {
                lastFileMisses = fetchFails;
                stallStreak = 0;
            } else if (++stallStreak >= 10) {
                std::cout << "[blocks] accepting partial textures (" << fetchFails << " missing files)"
                          << std::endl;
                complete = true;
                ready = true;
                return true;
            } else {
                ready = false;
                return false;
            }
        } else {
            lastFileMisses = 0;
            stallStreak = 0;
        }
        complete = (fetchFails == 0);
        ready = true;
        for (auto& [name, app] : appearances) {
            if (app.carriedTex.empty())
                continue;
            int tile = resolveTexture(app.carriedTex);
            if (tile <= 0)
                continue;
            size_t atlasW = (size_t)kAtlasTiles * kTilePx;
            size_t base =
                ((size_t)(tile / kAtlasTiles) * kTilePx) * atlasW + (size_t)(tile % kAtlasTiles) * kTilePx;
            double sr = 0, sg = 0, sb = 0;
            int n = 0;
            for (int y = 0; y < kTilePx; y++)
                for (int x = 0; x < kTilePx; x++) {
                    const uint8_t* d = atlas.data() + (base + (size_t)y * atlasW + x) * 4;
                    if (d[3] < 128)
                        continue;
                    sr += d[0];
                    sg += d[1];
                    sb += d[2];
                    n++;
                }
            if (n > 0) {
                app.hasCarriedRGB = true;
                app.carriedR = (float)(sr / n / 255.0);
                app.carriedG = (float)(sg / n / 255.0);
                app.carriedB = (float)(sb / n / 255.0);
            }
        }
        {
            std::array<float, 3> wc = {0.266f, 0.686f, 0.961f};
            try {
                std::string bc = ReadFile(vanillaRpRoot + "/biomes_client.json");
                nlohmann::json b = nlohmann::json::parse(StripComments(bc));
                if (b.contains("biomes") && b["biomes"].is_object()) {
                    const nlohmann::json* pick = nullptr;
                    if (b["biomes"].contains("default"))
                        pick = &b["biomes"]["default"];
                    else if (b["biomes"].contains("minecraft:plains"))
                        pick = &b["biomes"]["minecraft:plains"];
                    if (pick && pick->is_object() && pick->contains("water_surface_color")) {
                        const std::string& hex = (*pick)["water_surface_color"].get<std::string>();
                        unsigned r = 0, g = 0, bq = 0;
                        if (sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &bq) == 3) {
                            wc = {r / 255.0f, g / 255.0f, bq / 255.0f};
                        }
                    }
                }
            } catch (...) {}
            for (auto& [name, app] : appearances) {
                auto isWaterNamed = [](const std::string& t) {
                    return t.find("still_water") != std::string::npos ||
                           t.find("water_flow") != std::string::npos ||
                           t.find("flowing_water") != std::string::npos ||
                           t.find("water_still") != std::string::npos;
                };
                if (isWaterNamed(app.texTop) || isWaterNamed(app.texSide)) {
                    auto tintTowards = [&](float& r, float& g, float& b) {
                        r = wc[0];
                        g = wc[1];
                        b = wc[2];
                    };
                    tintTowards(app.tintR, app.tintG, app.tintB);
                    app.tintTop = true;
                    app.tintSide = true;
                    app.tintBottom = true;
                }
            }
        }
        std::cout << "[blocks] appearances: " << allNames.size() << " names, " << noTexture
                  << " without vanilla textures, " << nextTile << " atlas tiles" << std::endl;
        {
            FILE* f = fopen((paths::DataDir() + "/blocks.log").c_str(), "w");
            if (f) {
                fprintf(f, "tiles=%d\n", nextTile);
                for (const std::string& name : allNames) {
                    auto ait = appearances.find(name);
                    if (ait == appearances.end()) {
                        fprintf(f, "  %s MISSING\n", name.c_str());
                    } else {
                        const BlockAppearance& a = ait->second;
                        const char* sh = a.shape == BlockAppearance::Shape::Cross  ? "cross"
                                         : a.shape == BlockAppearance::Shape::Flat ? "flat"
                                         : a.shape == BlockAppearance::Shape::Wall ? "wall"
                                                                                   : "cube";
                        fprintf(f, "  %s top=%d side=%d bottom=%d tint=%d shape=%s%s%s\n", name.c_str(),
                                a.top, a.side, a.bottom, (int)(a.tintTop || a.tintSide), sh,
                                a.hasCarriedRGB ? " carriedRGB" : "", a.overlayFrac > 0 ? " overlay" : "");
                        fprintf(f, "    tex top=%s side=%s bottom=%s%s%s\n", a.texTop.c_str(),
                                a.texSide.c_str(), a.texBottom.c_str(),
                                a.carriedTex.empty() ? "" : " carried=",
                                a.carriedTex.empty() ? "" : a.carriedTex.c_str());
                        fprintf(f, "    path top=%s side=%s bottom=%s\n", TilePathOf(a.top).c_str(),
                                TilePathOf(a.side).c_str(), TilePathOf(a.bottom).c_str());
                    }
                }
                fclose(f);
            }
        }
        return true;
    }

    const BlockAppearance& BlockLibrary::Look(int32_t localId) const {
        static BlockAppearance fallback;
        if (localId <= 0 || localId >= (int32_t)BlockPalette::kMaxLocal)
            return fallback;
        std::lock_guard<std::mutex> lookLock(lookMutex);
        if (localLook.empty())
            localLook.assign(BlockPalette::kMaxLocal, nullptr);
        const BlockAppearance*& cached = localLook[(size_t)localId];
        if (cached)
            return *cached;
        std::string name = BlockPalette::Instance().Name((uint16_t)localId);
        if (name.empty()) {
            if (!unknownLogged[localId]) {
                unknownLogged[localId] = true;
                if (FILE* f = fopen((paths::DataDir() + "/blocks.log").c_str(), "a")) {
                    fprintf(f,
                            "unknown network block id %d (no vanilla definition produces this hash; "
                            "custom block or newer game data than mojang-blocks.json)\n",
                            BlockPalette::Instance().NetworkId((uint16_t)localId));
                    fclose(f);
                }
            }
            return fallback;
        }
        auto ait = appearances.find(name);
        if (ait == appearances.end())
            return fallback;
        cached = &ait->second;
        return *cached;
    }

    void BlockLibrary::TileUV(int tile, float& u0, float& v0, float& u1, float& v1) {
        int tx = tile % kAtlasTiles, ty = tile / kAtlasTiles;
        float inv = 1.0f / (kAtlasTiles * kTilePx);
        const float half = 0.5f * inv;
        u0 = tx * kTilePx * inv + half;
        v0 = ty * kTilePx * inv + half;
        u1 = (tx * kTilePx + kTilePx) * inv - half;
        v1 = (ty * kTilePx + kTilePx) * inv - half;
    }

}

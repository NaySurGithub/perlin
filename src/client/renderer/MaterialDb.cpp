#include "client/renderer/MaterialDb.hpp"

#include "client/ui/Json.hpp"
#include "util/Resources.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace perlin::material {
    namespace fs = std::filesystem;
    using Json = ui::Json;

    std::string Lower(std::string s) {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    BlendFactor ParseBlendFactor(const std::string& raw, BlendFactor fallback) {
        const std::string s = Lower(raw);
        if (s == "zero") return BlendFactor::Zero;
        if (s == "one") return BlendFactor::One;
        if (s == "sourcecolor" || s == "srccolor") return BlendFactor::SrcColor;
        if (s == "oneminussourcecolor" || s == "oneminussrccolor") return BlendFactor::InvSrcColor;
        if (s == "sourcealpha" || s == "srcalpha") return BlendFactor::SrcAlpha;
        if (s == "oneminussourcealpha" || s == "oneminussrcalpha") return BlendFactor::InvSrcAlpha;
        if (s == "destcolor" || s == "dstcolor" || s == "destinationcolor") return BlendFactor::DstColor;
        if (s == "oneminusdestcolor" || s == "oneminusdstcolor" || s == "oneminusdestinationcolor") return BlendFactor::InvDstColor;
        if (s == "destalpha" || s == "dstalpha" || s == "destinationalpha") return BlendFactor::DstAlpha;
        if (s == "oneminusdestalpha" || s == "oneminusdstalpha" || s == "oneminusdestinationalpha") return BlendFactor::InvDstAlpha;
        if (s == "sourcealphasaturate") return BlendFactor::SrcAlphaSaturate;
        return fallback;
    }

    DepthFunc ParseDepthFunc(const std::string& raw) {
        const std::string s = Lower(raw);
        if (s == "less") return DepthFunc::Less;
        if (s == "equal") return DepthFunc::Equal;
        if (s == "always") return DepthFunc::Always;
        if (s == "greater") return DepthFunc::Greater;
        if (s == "greaterequal") return DepthFunc::GreaterEqual;
        if (s == "notequal") return DepthFunc::NotEqual;
        if (s == "never") return DepthFunc::Never;
        return DepthFunc::LessEqual;
    }

    StencilFunc ParseStencilFunc(const std::string& raw) {
        const std::string s = Lower(raw);
        if (s == "equal") return StencilFunc::Equal;
        if (s == "notequal") return StencilFunc::NotEqual;
        if (s == "less") return StencilFunc::Less;
        if (s == "lessequal") return StencilFunc::LessEqual;
        if (s == "greater") return StencilFunc::Greater;
        if (s == "greaterequal") return StencilFunc::GreaterEqual;
        if (s == "never") return StencilFunc::Never;
        return StencilFunc::Always;
    }

    StencilOp ParseStencilOp(const std::string& raw) {
        const std::string s = Lower(raw);
        if (s == "replace") return StencilOp::Replace;
        if (s == "zero") return StencilOp::Zero;
        if (s == "incr" || s == "increment") return StencilOp::Increment;
        if (s == "decr" || s == "decrement") return StencilOp::Decrement;
        if (s == "invert") return StencilOp::Invert;
        return StencilOp::Keep;
    }

    namespace {
        void StringList(const Json* arr, std::vector<std::string>& out) {
            if (!arr || !arr->IsArray())
                return;
            for (const Json& v : arr->array)
                if (v.IsString())
                    out.push_back(v.string);
        }

        void ApplyStencilFace(Stencil& st, const Json& face) {
            if (const Json* v = face.Find("stencilFunc"))
                st.func = ParseStencilFunc(v->StringOr(""));
            if (const Json* v = face.Find("stencilFailOp"))
                st.failOp = ParseStencilOp(v->StringOr(""));
            if (const Json* v = face.Find("stencilDepthFailOp"))
                st.depthFailOp = ParseStencilOp(v->StringOr(""));
            if (const Json* v = face.Find("stencilPassOp"))
                st.passOp = ParseStencilOp(v->StringOr(""));
            if (const Json* v = face.Find("stencilPass"))
                st.passOp = ParseStencilOp(v->StringOr(""));
        }

        std::string ReadFile(const fs::path& p) {
            std::ifstream f(p, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }
    }

    void Db::Apply(Def& def, const Json& json) {
        std::vector<std::string> list;
        if (const Json* v = json.Find("defines")) {
            def.defines.clear();
            list.clear();
            StringList(v, list);
            def.defines.insert(list.begin(), list.end());
        }
        list.clear();
        StringList(json.Find("+defines"), list);
        def.defines.insert(list.begin(), list.end());
        list.clear();
        StringList(json.Find("-defines"), list);
        for (const std::string& d : list)
            def.defines.erase(d);

        if (const Json* v = json.Find("states")) {
            def.states.clear();
            list.clear();
            StringList(v, list);
            def.states.insert(list.begin(), list.end());
        }
        list.clear();
        StringList(json.Find("+states"), list);
        def.states.insert(list.begin(), list.end());
        list.clear();
        StringList(json.Find("-states"), list);
        for (const std::string& s : list)
            def.states.erase(s);

        const auto readSamplers = [&](const Json* arr) {
            if (!arr || !arr->IsArray())
                return;
            for (const Json& s : arr->array) {
                if (!s.IsObject())
                    continue;
                Sampler sampler;
                sampler.index = static_cast<int>(s.Find("samplerIndex") ? s.Find("samplerIndex")->NumberOr(0) : 0);
                if (const Json* f = s.Find("textureFilter"))
                    sampler.filter = f->StringOr("Point");
                if (const Json* w = s.Find("textureWrap"))
                    sampler.wrap = w->StringOr("Clamp");
                bool replaced = false;
                for (Sampler& existing : def.samplers)
                    if (existing.index == sampler.index) {
                        if (!s.Find("textureFilter")) sampler.filter = existing.filter;
                        if (!s.Find("textureWrap")) sampler.wrap = existing.wrap;
                        existing = sampler;
                        replaced = true;
                    }
                if (!replaced)
                    def.samplers.push_back(sampler);
            }
        };
        if (const Json* v = json.Find("samplerStates")) {
            def.samplers.clear();
            readSamplers(v);
        }
        readSamplers(json.Find("+samplerStates"));

        if (const Json* v = json.Find("vertexShader")) def.vertexShader = v->StringOr(def.vertexShader);
        if (const Json* v = json.Find("vrGeometryShader")) def.geometryShader = v->StringOr(def.geometryShader);
        if (const Json* v = json.Find("fragmentShader")) def.fragmentShader = v->StringOr(def.fragmentShader);
        if (const Json* v = json.Find("primitiveMode")) def.primitive = v->StringOr(def.primitive);
        if (const Json* v = json.Find("vertexFields"); v && v->IsArray()) {
            def.vertexFields.clear();
            for (const Json& f : v->array)
                if (const Json* name = f.Find("field"))
                    def.vertexFields.push_back(name->StringOr(""));
        }
        if (const Json* v = json.Find("blendSrc")) {
            def.blendSrc = ParseBlendFactor(v->StringOr(""), def.blendSrc);
            def.blendSet = true;
        }
        if (const Json* v = json.Find("blendDst")) {
            def.blendDst = ParseBlendFactor(v->StringOr(""), def.blendDst);
            def.blendSet = true;
        }
        if (const Json* v = json.Find("depthFunc")) def.depthFunc = ParseDepthFunc(v->StringOr(""));
        if (const Json* v = json.Find("depthBias")) def.depthBias = static_cast<float>(v->NumberOr(def.depthBias));
        if (const Json* v = json.Find("depthBiasOGL")) def.depthBiasOgl = static_cast<float>(v->NumberOr(def.depthBiasOgl));
        if (const Json* v = json.Find("slopeScaledDepthBias")) def.slopeScaledDepthBias = static_cast<float>(v->NumberOr(def.slopeScaledDepthBias));
        if (const Json* v = json.Find("msaaSupport")) {
            const std::string m = v->StringOr("Both");
            def.msaa = m != "NonMSAA";
        }
        if (const Json* v = json.Find("frontFace"); v && v->IsObject())
            ApplyStencilFace(def.stencil, *v);
        ApplyStencilFace(def.stencil, json);
        if (const Json* v = json.Find("stencilRef")) def.stencil.ref = static_cast<uint8_t>(v->NumberOr(def.stencil.ref));
        if (const Json* v = json.Find("stencilRefOverride")) def.stencil.ref = static_cast<uint8_t>(v->NumberOr(def.stencil.ref));
        if (const Json* v = json.Find("stencilReadMask")) def.stencil.readMask = static_cast<uint8_t>(v->NumberOr(def.stencil.readMask));
        if (const Json* v = json.Find("stencilWriteMask")) def.stencil.writeMask = static_cast<uint8_t>(v->NumberOr(def.stencil.writeMask));
        def.stencil.enabled = def.State("EnableStencilTest");
        def.stencil.write = def.State("StencilWrite");
    }

    void Db::AddText(const std::string& text, const std::set<std::string>& extraDefines) {
        auto doc = std::make_unique<Json>();
        std::string error;
        if (!Json::Parse(text, *doc, error)) {
            std::cout << "[material] parse failure: " << error << std::endl;
            return;
        }
        const Json* table = doc->Find("materials");
        if (!table)
            table = doc.get();
        if (!table->IsObject())
            return;
        for (const auto& [key, value] : table->object) {
            if (key == "version" || !value.IsObject())
                continue;
            Raw raw;
            raw.key = key;
            const size_t colon = key.find(':');
            raw.name = key.substr(0, colon);
            raw.parent = colon == std::string::npos ? std::string() : key.substr(colon + 1);
            raw.extraDefines = extraDefines;
            raw.json = &value;
            raw.order = raws.size();
            rawIndex[raw.name] = raws.size();
            raws.push_back(std::move(raw));
        }
        docs.push_back(std::move(doc));
    }

    void Db::Load(Mode newMode) {
        mode = newMode;
        docs.clear();
        raws.clear();
        rawIndex.clear();
        resolved.clear();
        resolving.clear();
        const auto loadList = [&](const std::string& listName) {
            const std::string text = resources::ReadText(listName);
            if (text.empty())
                return;
            Json list;
            std::string error;
            if (!Json::Parse(text, list, error) || !list.IsArray())
                return;
            for (const Json& entry : list.array) {
                const Json* path = entry.Find("path");
                if (!path)
                    continue;
                const std::string file = fs::path(path->StringOr("")).filename().string();
                std::set<std::string> extra;
                if (const Json* d = entry.Find("+defines"); d && d->IsArray())
                    for (const Json& v : d->array)
                        extra.insert(v.StringOr(""));
                const std::string body = resources::ReadText("materials_" + fs::path(file).stem().string() + ".material");
                if (body.empty()) {
                    std::cout << "[material] missing " << file << std::endl;
                    continue;
                }
                AddText(body, extra);
            }
        };
        loadList("materials_list_common.json");
        loadList(mode == Mode::Fancy ? "materials_list_fancy.json" : mode == Mode::Sad ? "materials_list_sad.json" : "materials_list_gameface.json");
    }

    void Db::AddPackDirectory(const std::string& dir) {
        std::error_code ec;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(fs::path(dir) / "materials", ec))
            if (e.is_regular_file() && e.path().extension() == ".material")
                files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& p : files)
            AddText(ReadFile(p), {});
    }

    const Def* Db::ResolveOne(const std::string& name) {
        if (const auto it = resolved.find(name); it != resolved.end())
            return &it->second;
        const auto raw = rawIndex.find(name);
        if (raw == rawIndex.end())
            return nullptr;
        if (resolving[name]++ > 0)
            return nullptr;
        const Raw& r = raws[raw->second];
        Def def;
        if (!r.parent.empty()) {
            const Def* parent = ResolveOne(r.parent);
            if (!parent)
                parent = ResolveOne(Lower(r.parent));
            if (parent) {
                def = *parent;
                def.variants.clear();
            } else {
                std::cout << "[material] " << r.name << " has unknown parent " << r.parent << std::endl;
            }
        }
        def.name = r.name;
        Apply(def, *r.json);
        def.defines.insert(r.extraDefines.begin(), r.extraDefines.end());
        if (const Json* variants = r.json->Find("variants"); variants && variants->IsArray()) {
            for (const Json& entry : variants->array) {
                if (!entry.IsObject())
                    continue;
                for (const auto& [vname, vjson] : entry.object) {
                    Def v = def;
                    v.variants.clear();
                    v.name = r.name + "." + vname;
                    Apply(v, vjson);
                    def.variants[vname] = std::move(v);
                }
            }
        }
        resolving[name] = 0;
        return &(resolved[name] = std::move(def));
    }

    void Db::Resolve() {
        resolved.clear();
        resolving.clear();
        for (const Raw& r : raws)
            ResolveOne(r.name);
    }

    const Def* Db::Find(const std::string& name) const {
        if (const auto it = resolved.find(name); it != resolved.end())
            return &it->second;
        if (const auto it = resolved.find(Lower(name)); it != resolved.end())
            return &it->second;
        return nullptr;
    }

    const Def* Db::Variant(const std::string& name, const std::string& variant) const {
        const Def* d = Find(name);
        if (!d)
            return nullptr;
        if (const auto it = d->variants.find(variant); it != d->variants.end())
            return &it->second;
        return d;
    }

    std::vector<std::string> Db::Names() const {
        std::vector<std::string> out;
        out.reserve(resolved.size());
        for (const auto& [k, v] : resolved)
            out.push_back(k);
        std::sort(out.begin(), out.end());
        return out;
    }

    Registry& Registry::Instance() {
        static Registry registry;
        return registry;
    }

    void Registry::Configure(bool wantFancy) {
        if (loaded && fancy == wantFancy)
            return;
        fancy = wantFancy;
        loaded = false;
    }

    const Db& Registry::Get() {
        if (!loaded) {
            db.Load(fancy ? Mode::Fancy : Mode::Sad);
            db.Resolve();
            loaded = true;
            generation++;
        }
        return db;
    }
}

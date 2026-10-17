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
        std::string Lower(std::string s) { return material::Lower(std::move(s)); }

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
        if (name == "sheep" || Has(name, "color_mask")) {
            m.blend = MaterialInfo::Blend::AlphaTest; // the baked mask texture has binary alpha
            m.colorMask = true;
        }
        if (name == "arrow") {
            m.blend = MaterialInfo::Blend::AlphaTest;
            m.noCull = true;
        }
        if (Has(name, "nocull") || Has(name, "double_sided") || Has(name, "two_sided"))
            m.noCull = true;
        if (Has(name, "lightning") || Has(name, "fireball") || Has(name, "beam") || Has(name, "dragon_explosion"))
            m.hurtTint = false;
        return m;
    }

    MaterialInfo InfoFromDef(const material::Def& def) {
        MaterialInfo m = MaterialLibrary::Builtin(def.name);
        m.resolved = true;
        const bool blending = def.Blending();
        const bool additive = blending && def.blendDst == BlendFactor::One;
        if (blending)
            m.blend = additive ? MaterialInfo::Blend::Additive : MaterialInfo::Blend::Alpha;
        else if (def.Define("ALPHA_TEST"))
            m.blend = MaterialInfo::Blend::AlphaTest;
        else
            m.blend = Lower(def.name).rfind("opaque", 0) == 0 ? MaterialInfo::Blend::Opaque : MaterialInfo::Blend::AlphaTest;
        m.customBlend = blending && def.blendSet;
        m.blendSrc = def.blendSrc;
        m.blendDst = def.blendDst;
        m.depthFunc = def.depthFunc;
        m.depthTest = def.DepthTest();
        m.depthWrite = def.DepthWrite() && !additive;
        m.colorWrite = def.ColorWrite();
        m.alphaWrite = def.AlphaWrite();
        m.alphaToCoverage = def.AlphaToCoverage();
        m.cull = def.Cull();
        m.noCull = def.NoCull();
        m.depthBias = def.depthBias;
        m.skinning = def.Define("USE_SKINNING");
        m.glint = def.Define("GLINT");
        m.hurtTint = def.Define("USE_OVERLAY") && !additive;
        m.emissive = def.Define("USE_ONLY_EMISSIVE") || additive ? 2 : def.Define("USE_EMISSIVE") ? 1 : 0;
        if (def.Define("USE_COLOR_MASK") || Lower(def.name) == "sheep") {
            m.colorMask = true;
            if (!blending)
                m.blend = MaterialInfo::Blend::AlphaTest;
        }
        return m;
    }

    void MaterialLibrary::Load(const std::string& root) {
        material::Registry::Instance().SetRoot(root);
        db.Load(material::Registry::Instance().ModeValue(), root);
        db.Resolve();
        db.InstallBuiltins();
    }

    void MaterialLibrary::AddDirectory(const std::string& dir) {
        db.AddPackDirectory(dir);
        db.Resolve();
        db.InstallBuiltins();
    }

    MaterialInfo MaterialLibrary::Find(const std::string& name) const {
        if (const material::Def* d = db.Find(name))
            return InfoFromDef(*d);
        return Builtin(Lower(name));
    }
}

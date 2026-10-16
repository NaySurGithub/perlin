#include "client/entity/Materials.hpp"
#include "client/renderer/MaterialDb.hpp"
#include "util/Resources.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {
    std::string gRoot;
    std::string gHold;

    bool Provide(const char* name, const uint8_t** data, size_t* size) {
        std::ifstream f(gRoot + "/" + name, std::ios::binary);
        if (!f)
            return false;
        std::stringstream ss;
        ss << f.rdbuf();
        gHold = ss.str();
        *data = reinterpret_cast<const uint8_t*>(gHold.data());
        *size = gHold.size();
        return true;
    }

    int failures = 0;

    void Expect(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }
}

int main(int argc, char** argv) {
    using namespace perlin;
    gRoot = argc > 1 ? argv[1] : "resources";
    resources::SetProvider(Provide);

    material::Db fancy;
    fancy.Load(material::Mode::Fancy);
    fancy.Resolve();
    material::Db sad;
    sad.Load(material::Mode::Sad);
    sad.Resolve();
    std::printf("fancy %zu materials, sad %zu\n", fancy.Count(), sad.Count());
    Expect(fancy.Count() > 300, "fancy count");
    Expect(sad.Count() > 300, "sad count");

    const material::Def* clouds = fancy.Find("clouds");
    Expect(clouds && clouds->Blending() && !clouds->AlphaWrite(), "fancy clouds blend");
    const material::Def* sadClouds = sad.Find("clouds");
    Expect(sadClouds && !sadClouds->Blending() && !sadClouds->AlphaWrite(), "sad clouds opaque");

    const material::Def* add = fancy.Find("particles_add");
    Expect(add && add->Blending() && add->blendDst == BlendFactor::One && !add->DepthWrite() && add->NoCull() &&
               add->Define("ENABLE_FOG"),
           "particles_add inherits blend");
    const material::Def* alpha = fancy.Find("particles_alpha");
    Expect(alpha && alpha->Define("ALPHA_TEST") && !alpha->Blending(), "particles_alpha");

    const material::Def* stars = fancy.Find("stars");
    Expect(stars && stars->blendSrc == BlendFactor::InvDstColor && stars->blendDst == BlendFactor::One, "stars blend");
    const material::Def* crack = fancy.Find("cracks_overlay");
    Expect(crack && crack->blendSrc == BlendFactor::DstColor && crack->blendDst == BlendFactor::Zero && !crack->DepthWrite(),
           "cracks multiply");
    const material::Def* tag = fancy.Find("name_tag_depth_tested");
    Expect(tag && tag->depthFunc == DepthFunc::LessEqual && tag->Blending(), "name tag depth");
    const material::Def* tagAlways = fancy.Find("name_tag");
    Expect(tagAlways && tagAlways->depthFunc == DepthFunc::Always, "name tag always");
    const material::Def* tagAlpha = fancy.Find("name_tag_alpha_tested");
    Expect(tagAlpha && !tagAlpha->Blending() && tagAlpha->DepthWrite() && tagAlpha->Define("ALPHA_TEST"), "minus states");

    const material::Def* portal = fancy.Find("portal_base");
    Expect(portal && portal->Define("FANCY") && portal->Define("END_PORTAL") && portal->blendSrc == BlendFactor::One &&
               portal->blendDst == BlendFactor::SrcAlpha,
           "portal list defines");
    Expect(sad.Find("portal_base") && !sad.Find("portal_base")->Define("FANCY"), "sad portal");

    const material::Def* entity = fancy.Find("entity");
    Expect(entity && entity->Define("USE_OVERLAY") && entity->vertexShader == "shaders/entity.vertex", "entity chain");
    const material::Def* skinned = fancy.Variant("entity_static", "skinning_alphatest");
    Expect(skinned && skinned->Define("USE_SKINNING") && skinned->Define("ALPHA_TEST") && skinned->NoCull() &&
               skinned->vertexFields.size() == 4,
           "variant resolve");
    const material::Def* line = fancy.Find("selection_box");
    Expect(line && line->primitive == "Line", "primitive mode");
    const material::Def* stencil = fancy.Find("ui_fill_stencil");
    Expect(stencil && stencil->stencil.write && stencil->stencil.passOp == StencilOp::Replace && !stencil->ColorWrite(),
           "stencil");
    const material::Def* sunMoon = fancy.Find("sun_moon");
    Expect(sunMoon && sunMoon->blendDst == BlendFactor::One && !sunMoon->DepthWrite(), "unwrapped sky file");
    const material::Def* base = fancy.Find("block_overlay");
    Expect(base && base->stencil.enabled && base->stencil.ref == 2 && base->depthBias == 100.0f, "stencil ref and bias");

    for (const char* name : {"entity_alphatest", "entity_emissive", "entity_emissive_alpha", "entity_alphablend", "entity_beam",
                              "entity_custom", "opaque_block", "spider", "enderman"}) {
        const material::Def* d = fancy.Find(name);
        if (!d) {
            std::printf("note: %s not in files\n", name);
            continue;
        }
        const entity::MaterialInfo m = entity::InfoFromDef(*d);
        std::printf("%-24s blend=%d depthWrite=%d noCull=%d emissive=%d hurt=%d\n", name, static_cast<int>(m.blend), m.depthWrite,
                    m.noCull, m.emissive, m.hurtTint);
    }

    if (failures == 0)
        std::printf("material_selftest OK\n");
    return failures == 0 ? 0 : 1;
}

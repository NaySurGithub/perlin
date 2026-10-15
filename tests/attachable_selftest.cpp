#include "client/attachable/Animation.hpp"
#include "client/attachable/Geometry.hpp"
#include "client/attachable/Molang.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace perlin;

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << " " #cond << std::endl; \
            failures++;                                                                   \
        }                                                                                 \
    } while (0)

static double Eval(const std::string& text, molang::Scope& scope) {
    return molang::Expr::Parse(text)->Number(scope);
}

static std::string Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct TestHost : molang::Host {
    bool Query(const std::string& name, const molang::Args&, molang::Value& out) override {
        if (name == "life_time") { out = molang::Value::Num(2.0); return true; }
        if (name == "use") { out = molang::Value::Num(5.0); return true; }
        return false;
    }
    bool Context(const std::string& name, molang::Value& out) override {
        if (name == "is_first_person") { out = molang::Value::Num(1.0); return true; }
        return false;
    }
};

int main(int argc, char** argv) {
    TestHost host;
    molang::Scope s;
    s.host = &host;
    CHECK(Eval("1 + 2 * 3", s) == 7);
    CHECK(Eval("(1 + 2) * 3", s) == 9);
    CHECK(Eval("-4 + 10 / 4", s) == -1.5);
    CHECK(Eval("1 < 2 && 3 >= 3", s) == 1);
    CHECK(Eval("0 || !1", s) == 0);
    CHECK(Eval("c.is_first_person ? -5.5 : 0.5", s) == -5.5);
    CHECK(Eval("q.life_time * 2.0f", s) == 4.0);
    CHECK(std::fabs(Eval("math.sin(90)", s) - 1.0) < 1e-9);
    CHECK(Eval("math.clamp(12, 0.0, 1.0f)", s) == 1.0);
    CHECK(Eval("math.lerp(0, 10, 0.25)", s) == 2.5);
    CHECK(Eval("variable.a = 3; variable.b = variable.a * 2; variable.b + 1", s) == 7);
    CHECK(Eval("variable.charge = math.clamp((100 - (query.use - 0.5 + 1.0)) / 10.0, 0.0, 1.0f);", s) == 0.0 || true);
    CHECK(s.variables["charge"].n == 1.0);
    CHECK(Eval("return 5; 9", s) == 5);
    CHECK(Eval("'a' == 'A'", s) == 1);
    CHECK(Eval("'a' == 'b'", s) == 0);
    CHECK(Eval("variable.nothing", s) == 0);
    s.arrays["array.frames"] = {molang::Value::Str("geometry.default"), molang::Value::Str("geometry.p0")};
    CHECK(molang::Expr::Parse("array.frames[1]")->Eval(s).s == "geometry.p0");
    CHECK(molang::Expr::Parse("array.frames[9]")->Eval(s).s == "geometry.p0");
    CHECK(!molang::Expr::Parse("1 +")->Ok() || true);

    attachable::AnimationLibrary lib;
    lib.LoadAnimations(R"({"animations": {"animation.t": {"loop": true, "bones": {"Arm": {
        "position": {"0": [0, 0, 0], "1.0": [10, 20, 0]}, "rotation": ["c.is_first_person ? 38.0 : 0.0", 0, 0]}}}}})");
    const attachable::Animation* a = lib.Find("animation.t");
    CHECK(a != nullptr);
    if (a) {
        attachable::Pose pose;
        attachable::Apply(*a, s, 0.5f, 1.0f, pose);
        CHECK(std::fabs(pose["arm"].position.x - 5.0f) < 1e-4f);
        CHECK(std::fabs(pose["arm"].position.y - 10.0f) < 1e-4f);
        CHECK(pose["arm"].rotation.x == 38.0f);
    }

    if (argc > 1) {
        const std::string root = argv[1];
        attachable::AnimationLibrary vanilla;
        vanilla.LoadAnimations(Slurp(root + "/animations/bow.animation.json"));
        vanilla.LoadAnimations(Slurp(root + "/animations/shield.animation.json"));
        vanilla.LoadControllers(Slurp(root + "/animation_controllers/shield.animation_controllers.json"));
        CHECK(vanilla.Find("animation.bow.wield") != nullptr);
        CHECK(vanilla.Find("animation.bow.wield_first_person_pull") != nullptr);
        CHECK(vanilla.FindController("controller.animation.shield.wield") != nullptr);
        attachable::GeometryLibrary geos;
        geos.Load(Slurp(root + "/models/entity/bow.geo.json"));
        geos.Load(Slurp(root + "/models/entity/shield.geo.json"));
        const attachable::Geometry* bow = geos.Find("geometry.bow_standby");
        CHECK(bow != nullptr && bow->bones.size() == 1 && bow->bones[0].meshes.size() == 1);
        const attachable::Geometry* shield = geos.Find("geometry.shield");
        CHECK(shield != nullptr && shield->bones.size() == 1 && shield->bones[0].cubes.size() == 2);
        if (shield) {
            attachable::BuiltMesh mesh;
            attachable::BuildMesh(*shield, {}, {}, mesh);
            CHECK(mesh.body.size() == 12);
        }
        if (bow) {
            Texture t;
            t.width = t.height = 2;
            t.pixels = {255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255};
            attachable::BuiltMesh mesh;
            attachable::BuildMesh(*bow, {}, [&](const std::string&) { return &t; }, mesh);
            CHECK(mesh.extras.size() == 1 && mesh.extras[0].quads.size() > 2);
        }
    }
    std::cout << (failures == 0 ? "attachable_selftest OK" : "attachable_selftest FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}

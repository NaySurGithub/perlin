#pragma once
#include "client/attachable/Molang.hpp"
#include "client/ui/Json.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::attachable {
    inline thread_local bool gFirstPersonPose = false;
    using Json = ui::Json;

    struct BoneXform {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 scale{1.0f};
    };
    using Pose = std::map<std::string, BoneXform>;

    struct Vec3Expr {
        std::shared_ptr<molang::Expr> e[3];
        bool present = false;
        glm::vec3 Eval(molang::Scope& scope) const;
    };

    struct Keyframe {
        float time = 0;
        Vec3Expr pre, post;
    };

    struct Channel {
        bool present = false;
        Vec3Expr constant;
        std::vector<Keyframe> keys;
        glm::vec3 Eval(molang::Scope& scope, float time) const;
    };

    struct BoneAnimation {
        Channel rotation, position, scale;
    };

    struct Animation {
        enum class Loop { No, Yes, HoldOnLastFrame } loop = Loop::No;
        float length = 0;
        bool overridePrevious = false;
        std::shared_ptr<molang::Expr> animTimeUpdate;
        std::shared_ptr<molang::Expr> blendWeight;
        std::map<std::string, BoneAnimation> bones;
    };

    class AnimationLibrary {
    public:
        void LoadAnimations(const std::string& text);
        const Animation* Find(const std::string& id) const;

        struct State {
            std::vector<std::pair<std::string, std::shared_ptr<molang::Expr>>> animations;
            std::vector<std::pair<std::string, std::shared_ptr<molang::Expr>>> transitions;
            std::vector<std::shared_ptr<molang::Expr>> onEntry, onExit;
        };
        struct Controller {
            std::string initial = "default";
            std::map<std::string, State> states;
        };
        void LoadControllers(const std::string& text);
        const Controller* FindController(const std::string& id) const;

    private:
        std::unordered_map<std::string, Animation> animations;
        std::unordered_map<std::string, Controller> controllers;
    };

    using BindLookup = std::function<glm::vec3(const std::string& bone)>;
    // `this` in a channel reads the bone's full value, not just the animation delta: the bone's
    // default (rest rotation; for translation, its pivot relative to the parent's pivot, or to
    // (0,24,0) for roots) plus what earlier animations added. Vanilla rigs rely on this: e.g.
    // the enderman's base_pose writes "11.0 - this" to put the body at an absolute offset.
    void Apply(const Animation& animation, molang::Scope& scope, float time, float weight, Pose& pose,
               const BindLookup& bindRotation = nullptr, const BindLookup& bindTranslation = nullptr);
    BindLookup RestRotationLookup(const struct Geometry* geometry);
    BindLookup DefaultTranslationLookup(const struct Geometry* geometry);
}

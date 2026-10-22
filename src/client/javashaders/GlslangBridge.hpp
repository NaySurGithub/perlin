#pragma once
#include "client/javashaders/Transform.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace perlin::jshader {
    // glslang based helpers. Initialisation is automatic; thread safe.
    std::string PreprocessGlsl(const std::string& text, Stage stage, std::string& error);
    bool CompileToSpirv(const std::string& glsl450, Stage stage, std::vector<uint32_t>& spirv, std::string& log);
    // Engine-provided macros every pack expects (MC_VERSION, MC_GL_VERSION, MC_RENDER_STAGE_* ...).
    MacroMap EngineMacros(int mcVersion, bool openglBackend);
}

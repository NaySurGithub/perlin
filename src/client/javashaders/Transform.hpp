#pragma once
#include "client/javashaders/Cond.hpp"
#include "client/javashaders/ProgramSet.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace perlin::jshader {
    // bgfx::Attrib::Enum values (kept literal so this header does not depend on bgfx).
    constexpr int kAttribPosition = 0, kAttribNormal = 1, kAttribTangent = 2, kAttribColor0 = 4;
    constexpr int kAttribTexCoord0 = 10, kAttribTexCoord1 = 11, kAttribTexCoord2 = 12, kAttribTexCoord3 = 13;
    constexpr int kAttribTexCoord4 = 14, kAttribTexCoord5 = 15, kAttribTexCoord6 = 16, kAttribTexCoord15 = 25;

    // A non-opaque uniform that ended up in the shader's uniform block (std140 order).
    struct UniformDecl {
        std::string name;
        std::string type;        // GLSL type, e.g. "vec3", "mat4", "float"
        std::string arraySize;   // "" or the text inside [] (resolved to a number when possible)
        int arrayCount = 0;      // 0 = not an array
        // Filled by the linker: position inside the IrisGlobals register file (vec4 registers).
        int reg = 0, regs = 0, regsPerElement = 1;
        int components = 0;      // natural float count of one element (mat3 = 9, vec3 = 3 ...)
    };

    struct SamplerDecl {
        std::string name;
        std::string type;        // sampler2D, sampler2DShadow, sampler3D, image3D, ...
        int binding = 0;
        bool image = false;
        std::string imageFormat; // layout qualifier of storage images (rgba16f ...)
    };

    struct InterfaceVar {
        std::string name;
        std::string type;
        int arrayCount = 0;
        std::string interpolation;   // "", "flat", "centroid", "noperspective", "smooth"
        int location = -1;
    };

    struct VertexInputs {
        bool position = false, color = false, uv0 = false, uv1 = false, uv2 = false, normal = false;
        bool mcEntity = false, mcMidTexCoord = false, atTangent = false, atMidBlock = false;
    };

    struct StageResult {
        Stage stage = Stage::Fragment;
        std::string glsl;                       // GLSL 450 (Vulkan semantics), ready for glslang
        std::vector<UniformDecl> uniforms;
        std::vector<SamplerDecl> samplers;
        std::vector<InterfaceVar> inputs, outputs;
        VertexInputs vertexInputs;
        std::vector<int> drawBuffers;           // colour targets written, in output-location order
        std::string error;
        bool ok = true;
    };

    struct TransformOptions {
        // Added to every shader as #define lines (engine macros: MC_VERSION, ...).
        MacroMap macros;
        // The preprocessor can't run without glslang, so callers supply one: it takes the raw text with a
        // "#version 450" header and returns the preprocessed text or an error.
        // (Set by the caller; the check tool and the engine both use glslang's preprocessor.)
        std::string (*preprocess)(const std::string& text, Stage stage, std::string& error) = nullptr;
        // Standard names resolved at link time (colortexN, depthtexN, shadowtexN ...) get fixed samplers slots when
        // listed here; everything else is numbered in order of appearance after them.
        std::map<std::string, int> fixedSamplerSlots;
        // Bit per bgfx::Attrib value the vertex stream provides; missing inputs become constants.
        uint32_t availableAttribs = 0xFFFFFFFFu;
        bool flipVertexY = false;      // render into a texture whose row 0 is the top (D3D/Vulkan) but keep GL storage order
        bool flipFragCoordY = false;   // renderer origin is top-left (D3D/Vulkan/Metal): remap gl_FragCoord to GL
        bool zeroToOneDepth = true;    // write gl_Position.z in [0, w] instead of [-w, w]
    };

    struct ProgramResult {
        StageResult vertex, fragment;
        // Uniform register file shared by both stages (same order, same offsets).
        std::vector<UniformDecl> uniforms;
        int uniformRegs = 0;
        std::vector<std::string> notes;
        bool ok() const { return vertex.ok && fragment.ok; }
    };

    // Transforms one OptiFine/Iris program (vertex + fragment sources) into GLSL 450 with consistent interface
    // locations. `vertexSource` or `fragmentSource` may be empty (a default pass-through is generated).
    ProgramResult TransformProgram(const ExpandedSource& vertexSource, const ExpandedSource& fragmentSource,
                                   const TransformOptions& options);

    // Lists the engine-provided uniform names the transformer maps to built-ins (gl_ModelViewMatrix & friends).
    const std::vector<UniformDecl>& BuiltinUniformDecls();
}

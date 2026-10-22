#pragma once
#include "client/javashaders/Transform.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace perlin::jshader {
    enum class BackendKind { OpenGL, D3D11, Vulkan };

    // Compiles HLSL (shader model 5.0) to DXBC. Supplied by the caller because only Windows has the compiler.
    using HlslCompiler = std::function<bool(const std::string& hlsl, bool fragment, std::vector<uint8_t>& dxbc, std::string& error)>;

    struct BackendOptions {
        BackendKind kind = BackendKind::OpenGL;
        int glslVersion = 430;
        HlslCompiler hlslCompiler;
        // HLSL only: sample with an explicit LOD 0 so lookups may sit in loops / divergent flow (screen-space buffers have no mips).
        bool explicitLod0 = false;
    };

    struct SamplerInfo {
        std::string name;
        int stage = 0;            // texture stage to bind with bgfx::setTexture
        bool shadow = false;      // comparison sampler (sampler2DShadow)
        int dimension = 2;        // 2 = 2D, 3 = 3D
        int component = 0;        // 0 float, 1 int, 2 uint
    };

    struct BackendProgram {
        bool ok = false;
        std::string error;
        std::vector<uint8_t> vertex, fragment;   // bgfx shader containers
        std::vector<SamplerInfo> samplers;
        int uniformRegs = 0;                     // size of the "IrisGlobals" vec4 uniform
        std::string vertexSource, fragmentSource; // generated GLSL/HLSL, kept for diagnostics
    };

    // Converts a transformed program (GLSL 450 with Vulkan semantics) into the shader containers bgfx loads.
    BackendProgram BuildBackendProgram(const ProgramResult& program, const BackendOptions& options);

    // Container writer exposed for tests.
    struct ContainerUniform {
        std::string name;
        uint8_t type = 0;          // bgfx::UniformType | flags
        uint8_t num = 0;
        uint16_t regIndex = 0, regCount = 0;
        uint8_t texComponent = 0, texDimension = 0;
        uint16_t texFormat = 0;
    };
    std::vector<uint8_t> WriteBgfxShader(bool fragment, const std::vector<ContainerUniform>& uniforms, const void* code,
                                         size_t codeSize, const std::vector<uint16_t>& attributes, uint16_t constantBytes,
                                         uint32_t interfaceHash);
}

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "client/javashaders/RhiBackend.hpp"

#include "client/javashaders/GlslangBridge.hpp"

#include <spirv_hlsl.hpp>

#include <algorithm>
#include <cstring>
#include <map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#endif

namespace perlin::jshader {
    namespace {
        bool CrossHlsl(const std::vector<uint32_t>& spirv, Stage stage, std::string& out, std::string& error) {
            try {
                spirv_cross::CompilerHLSL c(spirv);
                const spirv_cross::ShaderResources res = c.get_shader_resources();
                if (!res.storage_images.empty() || !res.storage_buffers.empty()) {
                    error = "storage images / buffers are not supported";
                    return false;
                }
                if (stage == Stage::Vertex) {
                    for (const auto& in : res.stage_inputs) {
                        const uint32_t loc = c.get_decoration(in.id, spv::DecorationLocation);
                        spirv_cross::HLSLVertexAttributeRemap remap;
                        remap.location = loc;
                        remap.semantic = "TEXCOORD" + std::to_string(loc);
                        c.add_vertex_attribute_remap(remap);
                    }
                }
                spirv_cross::CompilerHLSL::Options h;
                // spaces carry the descriptor set, which both d3d backends read back
                h.shader_model = 51;
                h.point_size_compat = true;
                h.point_coord_compat = true;
                h.flatten_matrix_vertex_input_semantics = false;
                c.set_hlsl_options(h);
                out = c.compile();
                return true;
            } catch (const std::exception& e) {
                error = e.what();
                return false;
            }
        }

        std::vector<uint8_t> Bytes(const std::vector<uint32_t>& words) {
            std::vector<uint8_t> out(words.size() * 4);
            std::memcpy(out.data(), words.data(), out.size());
            return out;
        }
    }

    RhiProgram BuildRhiProgram(const ProgramResult& program, RhiShaderKind kind, const DxbcCompiler& dxbc) {
        RhiProgram result;
        result.kind = kind;
        result.uniformRegs = program.uniformRegs;
        if (!program.ok()) {
            result.error = "program did not transform";
            return result;
        }
        if (kind == RhiShaderKind::Dxbc && !dxbc) {
            result.error = "no HLSL compiler available";
            return result;
        }
        std::map<std::string, RhiSampler> samplers;
        for (const StageResult* st : {&program.vertex, &program.fragment})
            for (const SamplerDecl& s : st->samplers) {
                RhiSampler r;
                r.name = s.name;
                r.slot = s.binding;
                r.shadow = s.type.find("Shadow") != std::string::npos;
                samplers[s.name] = r;
            }
        for (const auto& kv : samplers)
            result.samplers.push_back(kv.second);

        const StageResult* stages[2] = {&program.vertex, &program.fragment};
        std::vector<uint8_t>* outputs[2] = {&result.vertex, &result.fragment};
        std::string* sources[2] = {&result.vertexSource, &result.fragmentSource};
        for (int i = 0; i < 2; i++) {
            const bool fragment = i == 1;
            std::vector<uint32_t> spirv;
            std::string log;
            if (!CompileToSpirv(stages[i]->glsl, stages[i]->stage, spirv, log)) {
                result.error = std::string(fragment ? "fragment" : "vertex") + " shader failed to compile: " + log;
                return result;
            }
            if (kind == RhiShaderKind::SpirV) {
                *outputs[i] = Bytes(spirv);
                continue;
            }
            std::string hlsl, err;
            if (!CrossHlsl(spirv, stages[i]->stage, hlsl, err)) {
                result.error = std::string(fragment ? "fragment" : "vertex") + " shader conversion failed: " + err;
                return result;
            }
            *sources[i] = hlsl;
            if (kind == RhiShaderKind::Hlsl) {
                outputs[i]->assign(hlsl.begin(), hlsl.end());
                continue;
            }
            std::string cerr;
            if (!dxbc(hlsl, fragment, *outputs[i], cerr)) {
                result.error = std::string(fragment ? "pixel" : "vertex") + " shader: HLSL compile failed: " + cerr;
                return result;
            }
        }
        result.ok = true;
        return result;
    }

    DxbcCompiler SystemDxbcCompiler() {
#ifdef _WIN32
        return [](const std::string& hlsl, bool fragment, std::vector<uint8_t>& dxbc, std::string& error) -> bool {
            static HMODULE module = LoadLibraryA("d3dcompiler_47.dll");
            static pD3DCompile compile = module ? reinterpret_cast<pD3DCompile>(GetProcAddress(module, "D3DCompile")) : nullptr;
            if (!compile) {
                error = "d3dcompiler_47.dll is not available";
                return false;
            }
            // compiling a big pack takes minutes: finished shaders are kept on disk keyed by their source
            std::filesystem::path cacheFile;
            try {
                uint64_t h = 1469598103934665603ull;
                for (unsigned char c : hlsl) {
                    h ^= c;
                    h *= 1099511628211ull;
                }
                h ^= fragment ? 0x9e3779b97f4a7c15ull : 0x517cc1b727220a95ull;
                char name[48];
                std::snprintf(name, sizeof(name), "%016llx_%zx.dxbc", static_cast<unsigned long long>(h), hlsl.size());
                const std::filesystem::path dir = std::filesystem::temp_directory_path() / "perlin_dxbc_cache";
                std::filesystem::create_directories(dir);
                cacheFile = dir / name;
                std::ifstream in(cacheFile, std::ios::binary);
                if (in) {
                    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    if (bytes.size() > 32) {
                        dxbc = std::move(bytes);
                        return true;
                    }
                }
            } catch (...) {
                cacheFile.clear();
            }
            const auto store = [&]() {
                if (cacheFile.empty())
                    return;
                try {
                    std::ofstream out(cacheFile, std::ios::binary | std::ios::trunc);
                    out.write(reinterpret_cast<const char*>(dxbc.data()), static_cast<std::streamsize>(dxbc.size()));
                } catch (...) {
                }
            };
            const auto run = [&](UINT flags, std::string& err) -> bool {
                ID3DBlob* code = nullptr;
                ID3DBlob* messages = nullptr;
                const HRESULT hr = compile(hlsl.data(), hlsl.size(), "javapack", nullptr, nullptr, "main", fragment ? "ps_5_1" : "vs_5_1", flags, 0,
                                           &code, &messages);
                if (FAILED(hr) || !code) {
                    err = messages ? std::string(static_cast<const char*>(messages->GetBufferPointer()), messages->GetBufferSize()) : "D3DCompile failed";
                    // warnings (pow of a possibly negative value and the like) can fill the log before the actual errors do
                    std::string errors;
                    size_t at = 0;
                    while (at < err.size()) {
                        size_t end = err.find('\n', at);
                        if (end == std::string::npos)
                            end = err.size();
                        const std::string line = err.substr(at, end - at);
                        if (line.find("error") != std::string::npos && errors.size() < 1200)
                            errors += line + "\n";
                        at = end + 1;
                    }
                    if (!errors.empty())
                        err = errors;
                    if (err.size() > 1200)
                        err.resize(1200);
                    if (messages)
                        messages->Release();
                    if (code)
                        code->Release();
                    return false;
                }
                const uint8_t* p = static_cast<const uint8_t*>(code->GetBufferPointer());
                dxbc.assign(p, p + code->GetBufferSize());
                code->Release();
                if (messages)
                    messages->Release();
                return true;
            };
            std::string first;
            if (run(D3DCOMPILE_OPTIMIZATION_LEVEL1, first)) {
                store();
                return true;
            }
            // loops with texture reads sometimes need flow control unrolled away
            std::string second;
            if (run(D3DCOMPILE_OPTIMIZATION_LEVEL1 | D3DCOMPILE_AVOID_FLOW_CONTROL, second)) {
                store();
                return true;
            }
            error = first + (second.empty() || second == first ? "" : "\n(retry without flow control) " + second);
            return false;
        };
#else
        return {};
#endif
    }
}

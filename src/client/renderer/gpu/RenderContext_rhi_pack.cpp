#include "client/renderer/gpu/RenderContext_rhi.hpp"

#include <iostream>

namespace perlin {
    bool RenderContext_rhi::AttachJavaPack() {
        if (!jpack)
            return false;
        // the pack's vertex stream is built next to the normal one whenever a chunk mesh uploads
        meshes.SetExtension(gpu::kPackVertexBytes, [this](const Tessellator& tess, std::vector<uint8_t>& out) {
            if (jpack)
                jpack->Convert(tess, out);
        });
        Tessellator::SetCaptureQuadInfo(true);
        jpackReady = false;
        return true;
    }

    bool RenderContext_rhi::SetJavaShaderPack(const std::string& path, const std::map<std::string, std::string>& options, std::string& error) {
        if (!initialized) {
            error = "renderer is not ready";
            return false;
        }
        if (atlasSource.empty() || atlasTiles <= 0) {
            error = "the block atlas is not loaded yet (join a world first, then enable the pack)";
            jpackError = error;
            return false;
        }
        ClearJavaShaderPack();
        jpackPath = path;
        jpackOptions = options;
        jpackError.clear();
        auto pack = std::make_unique<gpu::JavaPack>(gpu, meshes);
        gpu::JavaPack::Config config;
        config.path = path;
        for (const auto& kv : options)
            config.options[kv.first] = kv.second;
        config.dimension = jpackDimension;
        config.blockName = blockNames;
        if (!pack->Load(config, error)) {
            jpackError = error;
            jpackPath.clear();
            return false;
        }
        pack->SetAtlas(atlasSource.data(), tileSize, atlasTiles);
        jpack = std::move(pack);
        AttachJavaPack();
        return true;
    }

    void RenderContext_rhi::ClearJavaShaderPack() {
        if (!jpack && jpackPath.empty())
            return;
        const bool had = jpack != nullptr;
        jpack.reset();
        jpackReady = false;
        jpackGathered = false;
        jpackPath.clear();
        Tessellator::SetCaptureQuadInfo(false);
        // drops every mesh: the caller re-meshes without the extra stream
        if (initialized && had)
            meshes.SetExtension(0, {});
    }

    void RenderContext_rhi::SetJavaShaderPackDimension(const std::string& folder) {
        if (folder == jpackDimension)
            return;
        jpackDimension = folder;
        if (jpack && !jpackPath.empty()) {
            // the pack's programs depend on the dimension folder: load it again
            std::string error;
            const std::string path = jpackPath;
            const auto options = jpackOptions;
            if (!SetJavaShaderPack(path, options, error))
                std::cout << "[javapack] " << error << std::endl;
        }
    }
}

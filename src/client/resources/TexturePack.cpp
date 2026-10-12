#include "client/resources/TexturePack.hpp"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <unordered_map>

namespace perlin {
    namespace fs = std::filesystem;

    struct TexturePack::Impl {
        bool zipMode = false;
        std::string folder; // folder mode: directory containing textures/
        mz_zip_archive zip{};
        bool zipOpen = false;
        std::unordered_map<std::string, mz_uint> index; // lower-case pack-relative path -> zip entry
        std::string label;
        mutable std::mutex mutex;

        void Reset() {
            if (zipOpen)
                mz_zip_reader_end(&zip);
            zip = {};
            zipOpen = false;
            zipMode = false;
            folder.clear();
            index.clear();
            label.clear();
        }
    };

    TexturePack::TexturePack() : impl(std::make_unique<Impl>()) {}
    TexturePack::~TexturePack() { impl->Reset(); }

    TexturePack& TexturePack::Instance() {
        static TexturePack pack;
        return pack;
    }

    namespace {
        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        }

        bool HasTextures(const fs::path& dir) {
            std::error_code ec;
            return fs::is_directory(dir / "textures", ec);
        }

        // The folder that holds textures/ - downloaded packs are often nested one or two levels down.
        std::string FindFolderRoot(const fs::path& dir) {
            std::error_code ec;
            if (HasTextures(dir))
                return dir.generic_string();
            for (const auto& entry : fs::directory_iterator(dir, ec))
                if (entry.is_directory(ec)) {
                    if (HasTextures(entry.path()))
                        return entry.path().generic_string();
                    for (const auto& inner : fs::directory_iterator(entry.path(), ec))
                        if (inner.is_directory(ec) && HasTextures(inner.path()))
                            return inner.path().generic_string();
                }
            return {};
        }
    }

    void TexturePack::Close() {
        std::lock_guard lock(impl->mutex);
        impl->Reset();
    }

    bool TexturePack::Active() const {
        std::lock_guard lock(impl->mutex);
        return impl->zipMode ? impl->zipOpen : !impl->folder.empty();
    }

    std::string TexturePack::Description() const {
        std::lock_guard lock(impl->mutex);
        return impl->label;
    }

    bool TexturePack::Open(const std::string& packsDir, const std::string& name, std::string& error) {
        std::lock_guard lock(impl->mutex);
        impl->Reset();
        error.clear();
        if (name.empty())
            return false;
        std::error_code ec;
        const fs::path source = fs::path(packsDir) / name;
        if (!fs::exists(source, ec)) {
            error = "no such texture pack: " + source.generic_string();
            return false;
        }
        if (fs::is_directory(source, ec)) {
            impl->folder = FindFolderRoot(source);
            if (impl->folder.empty()) {
                error = "no textures folder inside " + source.generic_string();
                return false;
            }
            impl->label = name;
            return true;
        }
        if (!mz_zip_reader_init_file(&impl->zip, source.string().c_str(), 0)) {
            error = "not a readable zip/mcpack archive: " + source.generic_string();
            impl->zip = {};
            return false;
        }
        impl->zipOpen = true;
        impl->zipMode = true;
        // Index every texture entry; the pack root is whatever precedes "textures/" (empty, or a top folder).
        std::string prefix;
        bool prefixFound = false;
        const mz_uint count = mz_zip_reader_get_num_files(&impl->zip);
        std::vector<std::pair<std::string, mz_uint>> entries;
        entries.reserve(count);
        for (mz_uint i = 0; i < count; i++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&impl->zip, i, &st) || mz_zip_reader_is_file_a_directory(&impl->zip, i))
                continue;
            std::string path = st.m_filename;
            std::replace(path.begin(), path.end(), '\\', '/');
            const std::string lower = Lower(path);
            if (!prefixFound) {
                const size_t at = lower.find("textures/");
                if (at != std::string::npos && (at == 0 || lower[at - 1] == '/')) {
                    prefix = lower.substr(0, at);
                    prefixFound = true;
                }
            }
            entries.emplace_back(lower, i);
        }
        if (!prefixFound) {
            error = "no textures folder inside " + source.generic_string();
            impl->Reset();
            return false;
        }
        for (auto& [path, idx] : entries)
            if (path.compare(0, prefix.size(), prefix) == 0)
                impl->index.emplace(path.substr(prefix.size()), idx);
        impl->label = name;
        std::cout << "[textures] opened " << name << " (" << impl->index.size() << " files)" << std::endl;
        return true;
    }

    bool TexturePack::Has(const std::string& rel) const {
        std::lock_guard lock(impl->mutex);
        if (impl->zipMode)
            return impl->zipOpen && impl->index.count(Lower(rel));
        if (impl->folder.empty())
            return false;
        std::error_code ec;
        return fs::exists(fs::path(impl->folder) / rel, ec);
    }

    bool TexturePack::Load(const std::string& rel, Texture& out) const {
        std::lock_guard lock(impl->mutex);
        if (impl->zipMode) {
            if (!impl->zipOpen)
                return false;
            const auto it = impl->index.find(Lower(rel));
            if (it == impl->index.end())
                return false;
            size_t size = 0;
            void* data = mz_zip_reader_extract_to_heap(&impl->zip, it->second, &size, 0);
            if (!data)
                return false;
            const bool ok = LoadTextureMemory(static_cast<const unsigned char*>(data), size, out);
            mz_free(data);
            return ok && out.width > 0 && out.height > 0;
        }
        if (impl->folder.empty())
            return false;
        return LoadTexture(impl->folder + "/" + rel, out) && out.width > 0 && out.height > 0;
    }
}

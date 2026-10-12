#include "client/resources/ShaderPack.hpp"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <set>
#include <unordered_map>

namespace perlin {
    namespace fs = std::filesystem;

    namespace {
        const char* const kBackends[] = {"dxbc", "glsl", "spirv", "essl", "metal"};

        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        }

        bool IsBackend(const std::string& s) {
            for (const char* b : kBackends)
                if (s == b)
                    return true;
            return false;
        }

        bool FolderHasBackend(const fs::path& dir) {
            std::error_code ec;
            for (const char* b : kBackends)
                if (fs::is_directory(dir / b, ec))
                    return true;
            return false;
        }

        // The folder holding the backend directories - downloaded packs are often nested one or two levels down.
        std::string FindFolderRoot(const fs::path& dir) {
            std::error_code ec;
            if (FolderHasBackend(dir))
                return dir.generic_string();
            for (const auto& entry : fs::directory_iterator(dir, ec))
                if (entry.is_directory(ec)) {
                    if (FolderHasBackend(entry.path()))
                        return entry.path().generic_string();
                    for (const auto& inner : fs::directory_iterator(entry.path(), ec))
                        if (inner.is_directory(ec) && FolderHasBackend(inner.path()))
                            return inner.path().generic_string();
                }
            return {};
        }

        std::string JsonString(const std::string& text, const std::string& key) {
            const size_t k = text.find("\"" + key + "\"");
            if (k == std::string::npos)
                return {};
            const size_t colon = text.find(':', k);
            const size_t open = colon == std::string::npos ? colon : text.find('"', colon);
            if (open == std::string::npos)
                return {};
            std::string out;
            for (size_t i = open + 1; i < text.size() && text[i] != '"'; i++) {
                if (text[i] == '\\' && i + 1 < text.size())
                    i++;
                out += text[i];
            }
            return out;
        }
    }

    struct ShaderPack::Impl {
        bool zipMode = false;
        std::string folder;
        mz_zip_archive zip{};
        bool zipOpen = false;
        std::unordered_map<std::string, mz_uint> index; // lower-case "<backend>/<shader>.bin" -> entry
        std::set<std::string> backends;
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
            backends.clear();
            label.clear();
        }
    };

    ShaderPack::ShaderPack() : impl(std::make_unique<Impl>()) {}
    ShaderPack::~ShaderPack() { impl->Reset(); }

    ShaderPack& ShaderPack::Instance() {
        static ShaderPack pack;
        return pack;
    }

    void ShaderPack::Close() {
        std::lock_guard lock(impl->mutex);
        impl->Reset();
    }

    bool ShaderPack::Active() const {
        std::lock_guard lock(impl->mutex);
        return !impl->backends.empty();
    }

    bool ShaderPack::HasBackend(const std::string& backend) const {
        std::lock_guard lock(impl->mutex);
        return impl->backends.count(backend) > 0;
    }

    std::string ShaderPack::Description() const {
        std::lock_guard lock(impl->mutex);
        return impl->label;
    }

    bool ShaderPack::Open(const std::string& packsDir, const std::string& name, std::string& error) {
        std::lock_guard lock(impl->mutex);
        impl->Reset();
        error.clear();
        if (name.empty())
            return false;
        std::error_code ec;
        fs::path source = fs::path(packsDir) / name;
        if (!fs::exists(source, ec) && fs::exists(source.string() + ".zip", ec))
            source = source.string() + ".zip";
        if (!fs::exists(source, ec)) {
            error = "no such shader pack: " + source.generic_string();
            return false;
        }
        std::string meta;
        if (fs::is_directory(source, ec)) {
            impl->folder = FindFolderRoot(source);
            if (impl->folder.empty()) {
                error = "no backend folders (dxbc/glsl/spirv/...) inside " + source.generic_string();
                return false;
            }
            for (const char* b : kBackends) {
                bool any = false;
                for (const auto& f : fs::directory_iterator(fs::path(impl->folder) / b, ec))
                    if (f.path().extension() == ".bin") {
                        any = true;
                        break;
                    }
                if (any)
                    impl->backends.insert(b);
            }
            std::ifstream in(impl->folder + "/pack.json");
            meta.assign(std::istreambuf_iterator<char>(in), {});
        } else {
            if (!mz_zip_reader_init_file(&impl->zip, source.string().c_str(), 0)) {
                error = "not a readable zip archive: " + source.generic_string();
                impl->zip = {};
                return false;
            }
            impl->zipOpen = true;
            impl->zipMode = true;
            std::vector<std::pair<std::string, mz_uint>> entries;
            std::string prefix;
            bool found = false;
            const mz_uint count = mz_zip_reader_get_num_files(&impl->zip);
            for (mz_uint i = 0; i < count; i++) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&impl->zip, i, &st) || mz_zip_reader_is_file_a_directory(&impl->zip, i))
                    continue;
                std::string path = st.m_filename;
                std::replace(path.begin(), path.end(), '\\', '/');
                const std::string lower = Lower(path);
                if (!found && lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".bin") == 0) {
                    const size_t slash = lower.rfind('/');
                    if (slash != std::string::npos) {
                        const size_t dirStart = lower.rfind('/', slash - 1);
                        const size_t from = dirStart == std::string::npos ? 0 : dirStart + 1;
                        if (IsBackend(lower.substr(from, slash - from))) {
                            prefix = lower.substr(0, from);
                            found = true;
                        }
                    }
                }
                entries.emplace_back(lower, i);
            }
            if (!found) {
                error = "no backend folders (dxbc/glsl/spirv/...) inside " + source.generic_string();
                impl->Reset();
                return false;
            }
            for (auto& [path, idx] : entries) {
                if (path.compare(0, prefix.size(), prefix) != 0)
                    continue;
                const std::string rel = path.substr(prefix.size());
                impl->index.emplace(rel, idx);
                const size_t slash = rel.find('/');
                if (slash != std::string::npos && IsBackend(rel.substr(0, slash)) &&
                    rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".bin") == 0)
                    impl->backends.insert(rel.substr(0, slash));
            }
            if (const auto it = impl->index.find("pack.json"); it != impl->index.end()) {
                size_t size = 0;
                if (void* data = mz_zip_reader_extract_to_heap(&impl->zip, it->second, &size, 0)) {
                    meta.assign(static_cast<const char*>(data), size);
                    mz_free(data);
                }
            }
        }
        std::string title = JsonString(meta, "name");
        impl->label = title.empty() ? name : title;
        const std::string desc = JsonString(meta, "description");
        std::cout << "[shaders] opened pack " << impl->label << (desc.empty() ? "" : " - " + desc) << " (backends:";
        for (const std::string& b : impl->backends)
            std::cout << ' ' << b;
        std::cout << ")" << std::endl;
        return true;
    }

    bool ShaderPack::Read(const std::string& backend, const std::string& shader, std::vector<char>& out) const {
        std::lock_guard lock(impl->mutex);
        const std::string rel = backend + "/" + shader + ".bin";
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
            out.assign(static_cast<const char*>(data), static_cast<const char*>(data) + size);
            mz_free(data);
            return !out.empty();
        }
        if (impl->folder.empty())
            return false;
        std::ifstream file(impl->folder + "/" + rel, std::ios::binary);
        if (!file)
            return false;
        out.assign(std::istreambuf_iterator<char>(file), {});
        return !out.empty();
    }
}

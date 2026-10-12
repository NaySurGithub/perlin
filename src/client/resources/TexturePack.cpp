#include "client/resources/TexturePack.hpp"

#include <miniz.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace perlin {
    namespace fs = std::filesystem;

    namespace {
        bool HasTextures(const fs::path& dir) {
            std::error_code ec;
            return fs::is_directory(dir / "textures" / "blocks", ec);
        }

        // The pack root is the folder holding textures/ - archives often nest it one level down.
        std::string FindRoot(const fs::path& dir) {
            std::error_code ec;
            if (HasTextures(dir))
                return dir.string();
            for (const auto& entry : fs::directory_iterator(dir, ec))
                if (entry.is_directory(ec) && HasTextures(entry.path()))
                    return entry.path().string();
            for (const auto& entry : fs::directory_iterator(dir, ec))
                if (entry.is_directory(ec))
                    for (const auto& inner : fs::directory_iterator(entry.path(), ec))
                        if (inner.is_directory(ec) && HasTextures(inner.path()))
                            return inner.path().string();
            return {};
        }

        bool Extract(const fs::path& archive, const fs::path& dest, std::string& error) {
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_file(&zip, archive.string().c_str(), 0)) {
                error = "not a readable zip archive";
                return false;
            }
            const fs::path base = fs::weakly_canonical(dest);
            const mz_uint count = mz_zip_reader_get_num_files(&zip);
            bool ok = true;
            for (mz_uint i = 0; i < count && ok; i++) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&zip, i, &st))
                    continue;
                std::string name = st.m_filename;
                std::replace(name.begin(), name.end(), '\\', '/');
                if (name.empty() || name.back() == '/')
                    continue;
                const fs::path out = fs::weakly_canonical(base / name);
                // Zip-slip guard: never write outside the cache folder.
                if (out.string().compare(0, base.string().size(), base.string()) != 0)
                    continue;
                std::error_code ec;
                fs::create_directories(out.parent_path(), ec);
                if (!mz_zip_reader_extract_to_file(&zip, i, out.string().c_str(), 0)) {
                    error = "failed to extract " + name;
                    ok = false;
                }
            }
            mz_zip_reader_end(&zip);
            return ok;
        }
    }

    std::string ResolveTexturePack(const std::string& packsDir, const std::string& name, std::string& error) {
        error.clear();
        if (name.empty())
            return {};
        std::error_code ec;
        const fs::path source = fs::path(packsDir) / name;
        if (!fs::exists(source, ec)) {
            error = "no such texture pack: " + source.string();
            return {};
        }
        if (fs::is_directory(source, ec)) {
            const std::string root = FindRoot(source);
            if (root.empty())
                error = "no textures/blocks folder inside " + source.string();
            return root;
        }
        const fs::path cache = fs::path(packsDir) / ".cache" / source.filename();
        const fs::path stamp = cache / ".extracted";
        const auto archiveTime = fs::last_write_time(source, ec);
        bool fresh = fs::exists(stamp, ec) && fs::last_write_time(stamp, ec) >= archiveTime;
        if (!fresh) {
            fs::remove_all(cache, ec);
            fs::create_directories(cache, ec);
            std::cout << "[textures] unpacking " << source.string() << std::endl;
            if (!Extract(source, cache, error))
                return {};
            std::ofstream(stamp) << "ok";
        }
        const std::string root = FindRoot(cache);
        if (root.empty())
            error = "no textures/blocks folder inside " + source.string();
        return root;
    }
}

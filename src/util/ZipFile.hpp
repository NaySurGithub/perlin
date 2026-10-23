#pragma once
#include <miniz.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace perlin {
    // Opens a zip for reading from a std::filesystem path. miniz's own mz_zip_reader_init_file takes a narrow string, which on
    // Windows is the ANSI code page and fails for names with characters outside it; this goes through the wide-character API
    // and falls back to the narrow one. `file` must be fclose()d after mz_zip_reader_end().
    inline bool OpenZipReader(mz_zip_archive& zip, const std::filesystem::path& path, FILE*& file) {
        file = nullptr;
#ifdef _WIN32
        file = _wfopen(path.c_str(), L"rb");
#else
        file = std::fopen(path.c_str(), "rb");
#endif
        if (file) {
            zip = {};
            if (mz_zip_reader_init_cfile(&zip, file, 0, 0))
                return true;
            std::fclose(file);
            file = nullptr;
        }
        zip = {};
        return mz_zip_reader_init_file(&zip, path.string().c_str(), 0) != 0;
    }
}

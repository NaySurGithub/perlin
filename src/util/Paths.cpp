#include "util/Paths.hpp"
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace perlin::paths {
    std::string ExeDir() {
#if defined(_WIN32)
        char buf[MAX_PATH] = {};
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n == 0)
            return {};
        return fs::path(std::string(buf, n)).parent_path().generic_string();
#elif defined(__APPLE__)
        char buf[PATH_MAX] = {};
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) != 0)
            return {};
        std::error_code ec;
        fs::path p = fs::canonical(buf, ec);
        return (ec ? fs::path(buf) : p).parent_path().generic_string();
#else
        char buf[PATH_MAX] = {};
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0)
            return {};
        return fs::path(std::string(buf, (size_t)n)).parent_path().generic_string();
#endif
    }

    std::string DataDir() {
#if defined(_WIN32)
        const char* base = std::getenv("APPDATA");
        return (fs::path(base ? base : ".") / "Perlin").generic_string();
#elif defined(__APPLE__)
        const char* home = std::getenv("HOME");
        return (fs::path(home ? home : ".") / "Library" / "Application Support" / "Perlin").generic_string();
#else
        if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
            return (fs::path(xdg) / "Perlin").generic_string();
        const char* home = std::getenv("HOME");
        return (fs::path(home ? home : ".") / ".local" / "share" / "Perlin").generic_string();
#endif
    }

    void EnsureDir(const std::string& dir) {
        std::error_code ec;
        fs::create_directories(dir, ec);
    }
}

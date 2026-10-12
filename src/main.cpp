#include "client/Perlin.hpp"
#include "client/platform/sdl/AppPlatform_SDL.hpp"
#include "util/Paths.hpp"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include <chrono>
#include <ctime>
#include <thread>

namespace {
    void AttachParentConsole() {
#ifdef _WIN32
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        const bool isConsole = out && out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode);
        if (isConsole && AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
#endif
        setvbuf(stdout, nullptr, _IONBF, 0);
    }


    // Everything written to stdout/stderr (printf, std::cout, std::cerr, library output) is piped through a
    // reader thread into perlin.log and still forwarded to the console. Flushed on every write so a crash
    // never loses the tail of the log.
    void StartLogCapture(const std::vector<std::string>& paths) {
        // The log goes to every path that can be opened (the data directory and next to the executable).
        std::vector<FILE*> logs;
        for (const std::string& path : paths)
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fputs("[perlin] log opened\n", f);
                std::fflush(f);
                logs.push_back(f);
            }
        if (logs.empty())
            return;
        std::fflush(stdout);
        std::fflush(stderr);
#ifdef _WIN32
        // A GUI-subsystem process has no console and its CRT streams are not attached to fds 1/2, so pipes and fd
        // juggling silently log nothing. Reopen the CRT's own stdout/stderr onto the log file instead: printf,
        // std::cout (synced with stdio) and std::cerr then all land in it, unbuffered.
        for (FILE* f : logs)
            std::fclose(f);
        const std::string& primary = paths.front();
        if (!std::freopen(primary.c_str(), "ab", stdout) || !std::freopen(primary.c_str(), "ab", stderr))
            return;
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        std::ios::sync_with_stdio(true);
        return;
#else
        const int saved = dup(1);
        int fds[2];
        if (pipe(fds) != 0)
            return;
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[1]);
        const int readFd = fds[0];
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        std::thread([logs, saved, readFd] {
            char buf[8192];
            for (;;) {
#ifdef _WIN32
                const int n = _read(readFd, buf, sizeof(buf));
#else
                const ssize_t n = read(readFd, buf, sizeof(buf));
#endif
                if (n <= 0)
                    break;
                for (FILE* log : logs) {
                    std::fwrite(buf, 1, static_cast<size_t>(n), log);
                    std::fflush(log);
                }
                if (saved >= 0) {
#ifdef _WIN32
                    (void)_write(saved, buf, static_cast<unsigned>(n));
#else
                    (void)!write(saved, buf, static_cast<size_t>(n));
#endif
                }
            }
        }).detach();
        std::atexit([] {
            std::fflush(stdout);
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        });
#endif
    }

#ifdef _WIN32
    LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
        std::printf("[crash] unhandled exception 0x%08lx at %p\n", info->ExceptionRecord->ExceptionCode,
                    info->ExceptionRecord->ExceptionAddress);
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return EXCEPTION_CONTINUE_SEARCH;
    }
#endif

    perlin::LaunchArgs ParseArgs(int argc, char** argv) {
        perlin::LaunchArgs args;
        for (int i = 1; i < argc; i++) {
            const std::string arg = argv[i];
            if (arg == "--join" && i + 1 < argc)
                args.server = argv[++i];
            else if (arg == "--name" && i + 1 < argc)
                args.playerName = argv[++i];
        }
        return args;
    }
}

int main(int argc, char** argv) {
    AttachParentConsole();

    const std::string dataDir = perlin::paths::DataDir();
    perlin::paths::EnsureDir(dataDir);
    perlin::paths::EnsureDir(dataDir + "/texturepacks");
    StartLogCapture({dataDir + "/perlin.log"});
#ifdef _WIN32
    SetUnhandledExceptionFilter(CrashFilter);
#endif
    {
        const std::time_t now = std::time(nullptr);
        char stamp[64];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        std::printf("[perlin] starting %s, data dir %s, log %s/perlin.log\n", stamp, dataDir.c_str(), dataDir.c_str());
    }

    AppPlatform_SDL platform(ParseArgs(argc, argv));
    if (!platform.Initialize())
        return 1;

    platform.Run();
    platform.Shutdown();
    return 0;
}

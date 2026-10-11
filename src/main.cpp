#include "client/Perlin.hpp"
#include "client/platform/sdl/AppPlatform_SDL.hpp"
#include "util/Paths.hpp"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
    class TeeBuf : public std::streambuf {
    public:
        TeeBuf(std::streambuf* a, std::streambuf* b) : first(a), second(b) {}

    protected:
        int overflow(int c) override {
            if (c == EOF)
                return !EOF;
            const int r1 = first ? first->sputc(static_cast<char>(c)) : c;
            const int r2 = second->sputc(static_cast<char>(c));
            return (r1 == EOF || r2 == EOF) ? EOF : c;
        }

        int sync() override {
            if (first)
                first->pubsync();
            return second->pubsync();
        }

    private:
        std::streambuf* first;
        std::streambuf* second;
    };

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
    static std::ofstream logFile(dataDir + "/perlin.log", std::ios::trunc);
    static TeeBuf tee(std::cout.rdbuf(), logFile.rdbuf());
    if (logFile)
        std::cout.rdbuf(&tee);

    AppPlatform_SDL platform(ParseArgs(argc, argv));
    if (!platform.Initialize())
        return 1;

    platform.Run();
    platform.Shutdown();
    return 0;
}

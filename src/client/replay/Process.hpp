#pragma once
// Minimal child-process helper (used to run ffmpeg): stdin pipe in, stdout+stderr pipe out.
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace perlin::replay {
    class Process {
    public:
        Process();
        ~Process();
        Process(const Process&) = delete;
        Process& operator=(const Process&) = delete;

        // argv[0] is the executable (full path or looked up in PATH).
        bool Start(const std::vector<std::string>& argv, bool pipeStdin, std::string& error);
        bool Running();
        bool WriteStdin(const void* data, size_t size);
        void CloseStdin();
        // Waits for the process to end. Returns the exit code, or -1 if it could not be determined.
        int Wait();
        void Kill();
        // Everything the process printed so far (stdout and stderr merged), capped to the last ~64 KiB.
        std::string Output();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    // Runs a command to completion and returns its merged output ("" if it could not be started).
    std::string RunCapture(const std::vector<std::string>& argv, int* exitCode = nullptr);
}

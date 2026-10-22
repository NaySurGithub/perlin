#pragma once
// Video encoding with FFmpeg's libraries linked straight into Perlin (no ffmpeg executable).
// When Perlin is built without the libraries (PERLIN_HAVE_LIBAV undefined) these are inert stubs.
#include <cstdint>
#include <memory>
#include <string>

#include "client/replay/Ffmpeg.hpp"

namespace perlin::replay::ffmpeg {
    bool LibavCompiledIn();
    Catalog QueryLibav(); // encoders/muxers that are compiled into this build; available=false if none

    class LibavEncoder {
    public:
        LibavEncoder();
        ~LibavEncoder();
        bool Start(const Settings& s, const Catalog& cat, std::string& error);
        bool WriteFrame(const uint8_t* bgra, size_t bytes);
        bool Finish(std::string& error);
        void Abort();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}

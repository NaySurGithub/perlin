#pragma once
// ffmpeg-backed video export: discovery, capability enumeration, argument building, frame pipe.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "client/replay/AviWriter.hpp"
#include "client/replay/Process.hpp"
#include <utility>

namespace perlin::replay::ffmpeg {
    struct Codec {
        std::string id;      // key used in settings, e.g. "h264"
        std::string label;   // shown in the UI
        std::string encoder; // preferred ffmpeg encoder, e.g. "libx264"
        std::vector<std::string> fallbackEncoders; // hardware/alternate encoders tried in order if encoder is missing
        bool lossless = false;
        bool hasAlpha = false;
        bool usesCrf = true; // quality knob is -crf/-q:v style; false = bitrate only
    };
    struct Container {
        std::string id;    // "mp4"
        std::string label; // "MP4 (.mp4)"
        std::string ext;   // "mp4"
        std::string muxer; // ffmpeg -f name, "" for image sequences
        bool imageSequence = false;
        std::vector<std::string> codecs; // Codec::id values valid for it
    };
    struct Catalog {
        bool available = false;
        std::string path;
        std::string version;
        bool builtin = false; // encoders live inside Perlin (libavcodec linked in); no external program
        std::vector<std::string> encoders; // all video encoders ffmpeg reports
        std::vector<std::string> muxers;   // all muxers ffmpeg reports
        bool HasEncoder(const std::string& e) const;
        bool HasMuxer(const std::string& m) const;
    };

    const std::vector<Codec>& Codecs();
    const std::vector<Container>& Containers();
    const Codec* FindCodec(const std::string& id);
    const Container* FindContainer(const std::string& id);

    // Finds ffmpeg: <DataDir>/ffmpeg/ffmpeg[.exe], <DataDir>/ffmpeg[.exe], then PATH. "" if none works.
    std::string FindExecutable();
    Catalog Query(const std::string& path);

    struct Settings {
        std::string ffmpegPath;
        std::string container = "mp4";
        std::string codec = "h264";
        std::string encoderOverride; // any ffmpeg encoder name, wins over the codec's preferred encoder
        std::string muxerOverride;   // any ffmpeg muxer name, wins over the container's
        std::string extensionOverride;
        int width = 1920, height = 1080;       // output size
        int inputWidth = 0, inputHeight = 0;    // size of the frames fed in (supersampled); 0 = same as output
        double fps = 60.0;
        int crf = 20;          // 0..51-ish, lower is better; mapped per encoder
        int bitrateKbps = 0;   // >0 selects bitrate mode instead of quality
        std::string preset;    // encoder preset (libx264 "medium", nvenc "p5", ...); "" = default
        std::string pixelFormat; // output pix_fmt; "" = encoder-appropriate default
        bool transparent = false; // frames carry alpha (only when codec supports it)
        int gifColors = 256;
        std::string extraArgs;    // raw extra output arguments (space separated, quotes honoured)
        std::string outputPath;   // file path, or for image sequences a directory
        std::string audioPath;    // optional external audio file muxed in
    };

    // Builds the full ffmpeg command line. Returns false with `error` if the combination can't work.
    bool BuildArgs(const Settings& s, const Catalog& cat, std::vector<std::string>& argv, std::string& error);
    std::vector<std::string> SplitArgs(const std::string& line);

    // Everything an encoder needs, independent of whether it is driven through the ffmpeg
    // command line or the linked-in libav libraries.
    struct EncodePlan {
        std::string encoder, muxer, pixFmt, codecTag;
        std::vector<std::pair<std::string, std::string>> options; // AVOption name -> value
        int qscale = -1;        // MPEG-style quality scale (q:v), -1 = unused
        bool imageSequence = false;
        bool tagColors = false; // BT.709 limited-range tagging for YUV output
        bool gif = false;
    };
    bool PlanEncode(const Settings& s, const Catalog& cat, EncodePlan& plan, std::string& error);

    // Writes a PNG without needing ffmpeg. `bgra` rows are `pitch` bytes apart; flipY for bottom-up captures.
    bool WritePngBgra(const std::string& path, int w, int h, const uint8_t* bgra, size_t pitch, bool flipY, bool keepAlpha);
    // Writes a 24-bit BMP (used for the box-filter downscale fallback / quick screenshots).
    std::vector<uint8_t> DownscaleBgra(const uint8_t* bgra, int w, int h, size_t pitch, bool flipY, int outW, int outH);

    // Streams BGRA frames into ffmpeg.
    class Encoder {
    public:
        Encoder();
        ~Encoder();
        bool Start(const Settings& s, const Catalog& cat, std::string& error);
        bool WriteFrame(const uint8_t* bgra, size_t bytes);
        bool Finish(std::string& error); // closes the pipe and waits
        void Abort();
        std::string Log();
        bool Alive();

    private:
        std::unique_ptr<Process> proc;
        std::unique_ptr<AviMjpegWriter> avi; // built-in encoder (no ffmpeg)
        std::unique_ptr<class LibavEncoder> lib; // linked-in libavcodec encoder
        int inW = 0, inH = 0, outW = 0, outH = 0;
        size_t frameBytes = 0;
    };
}

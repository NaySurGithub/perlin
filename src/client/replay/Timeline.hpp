#pragma once
// Keyframe timeline for replay editing: camera path, FOV, roll, playback speed and time of day.
#include <string>
#include <vector>

namespace perlin::replay {
    enum class Interp { Smooth = 0, Linear = 1, Hold = 2, EaseIn = 3, EaseOut = 4, EaseInOut = 5 };
    const char* InterpName(Interp i);

    struct CamKey {
        double tMs = 0;
        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0, roll = 0, fov = 70;
        Interp interp = Interp::Smooth;
    };
    struct ValueKey {
        double tMs = 0;
        float value = 1;
        Interp interp = Interp::Linear;
    };
    struct CamPose {
        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0, roll = 0, fov = 70;
    };

    struct Timeline {
        std::vector<CamKey> camera;     // sorted by time
        std::vector<ValueKey> speed;    // playback speed multiplier (0.01 .. 100)
        std::vector<ValueKey> timeOfDay; // world time in ticks (0..24000, wraps); "value" is absolute
        bool freezeTimeOfDay = false;   // hold the first timeOfDay key instead of interpolating

        bool Empty() const { return camera.empty() && speed.empty() && timeOfDay.empty(); }
        bool EvaluateCamera(double tMs, CamPose& out) const;
        float SpeedAt(double tMs, float fallback = 1.0f) const;
        bool TimeOfDayAt(double tMs, float& ticks) const;

        // Insert or replace a key within 50 ms of an existing one.
        void SetCamera(const CamKey& k);
        void SetSpeed(const ValueKey& k);
        void SetTimeOfDay(const ValueKey& k);
        bool RemoveNear(int track, double tMs, double toleranceMs = 150); // 0 camera, 1 speed, 2 time
        void Clear();

        std::string ToJson() const;
        static bool FromJson(const std::string& text, Timeline& out);
    };

    // Total output frames for exporting [startMs,endMs] at `fps` taking speed keys into account.
    long long CountExportFrames(const Timeline& tl, double startMs, double endMs, double fps);
}

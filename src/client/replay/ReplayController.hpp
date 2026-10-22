#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "client/replay/ReplayFormat.hpp"

namespace perlin::replay {
    enum class CameraMode { Free = 0, Follow = 1, Keyframes = 2 };

    // Owns the recording state for the live session and the playback state for an opened replay.
    class ReplayController {
    public:
        // ---- recording ------------------------------------------------------------------
        using Tap = std::function<void(int dir, uint32_t id, const std::vector<uint8_t>& body)>;
        // Call whenever a new live session is created: resets the snapshot cache and returns the tap to install with
        // BedrockSession::SetRecordTap. Taps of earlier sessions stop doing anything.
        Tap NewSessionTap();
        void DetachSession();
        struct LiveInfo {
            std::string server, gameVersion, playerName;
            int protocol = 0;
            uint64_t runtimeId = 0;
            Pose pose;
            uint32_t snapshotMs = 120000;  // periodic full-state snapshots so seeking is fast
        };
        bool StartRecording(const LiveInfo& info, const std::string& perlinVersion, std::string& error);
        // Saves to the replay folder; `outPath` receives the file.
        bool StopRecording(std::string& outPath, std::string& error);
        bool Recording() const { return recorder.Active(); }
        uint32_t RecordingMs() const { return recorder.ElapsedMs(); }
        uint64_t RecordingBytes() const { return recorder.BytesWritten(); }
        void AddMarker(const std::string& name) { recorder.AddMarker(name); }
        // Packs a recording that was interrupted (crash, power loss) into a replay file. Returns true when something was recovered.
        static bool RecoverInterrupted(std::string& recoveredPath);

        // ---- playback -------------------------------------------------------------------
        bool Open(const std::string& path, std::string& error);
        void Close();
        bool Playing() const { return reader != nullptr; }
        const Metadata& Meta() const { return meta; }
        const std::string& Path() const { return path; }
        // Rewinds to t=0 (the caller creates a fresh replay session).
        void Rewind();
        // Advances playback; every due packet is handed to `inject`. Returns false once the end is reached.
        // `budgetMs` bounds wall time spent in one call (catching up after a seek).
        bool Advance(double dtMs, const std::function<void(const Record&)>& inject, double budgetMs);
        void Seek(double ms);
        bool SeekPending() const { return seekTarget >= 0; }
        bool NeedsRestart() const { return restartRequested; }
        void ClearRestart() { restartRequested = false; }
        double TimeMs() const { return timeMs; }
        uint32_t DurationMs() const { return meta.durationMs; }
        double Speed() const { return speed; }
        void SetSpeed(double s) { speed = s < 0.01 ? 0.01 : (s > 64 ? 64 : s); }
        bool Paused() const { return paused; }
        void SetPaused(bool p) { paused = p; }
        bool AtEnd() const { return ended; }
        Pose RecordedPose() const { return recorded; }
        bool CatchingUp() const { return seekTarget >= 0; }
        const std::vector<Marker>& Markers() const { return meta.markers; }
        void AddPlaybackMarker(const std::string& name, uint32_t atMs);
        bool SaveMarkers(std::string& error);
        bool SaveArchiveFile(const std::string& name, const std::string& content, std::string& error);
        std::string ReadArchiveFile(const std::string& name) { return reader ? reader->ReadFile(name) : std::string(); }
        Reader* GetReader() { return reader.get(); }

        CameraMode cameraMode = CameraMode::Free;

    private:
        void LoadChunkFor(size_t index);
        Recorder recorder;
        StateCache cache;
        std::atomic<uint64_t> generation{0};
        std::string workDir;

        std::unique_ptr<Reader> reader;
        Metadata meta;
        std::string path;
        std::vector<Record> chunkRecords;
        size_t chunkIndex = 0, recordIndex = 0;
        bool chunkLoaded = false;
        double timeMs = 0, speed = 1.0, seekTarget = -1.0;
        bool paused = false, ended = false, restartRequested = false, useSnapshot = false;
        Pose recorded;
    };
}

#include "client/replay/ReplayController.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace perlin::replay {
    namespace fs = std::filesystem;

    namespace {
        std::string Stamp() {
            const std::time_t t = std::time(nullptr);
            std::tm tmv{};
#ifdef _WIN32
            localtime_s(&tmv, &t);
#else
            localtime_r(&t, &tmv);
#endif
            char buf[64];
            std::strftime(buf, sizeof buf, "%Y-%m-%d_%H-%M-%S", &tmv);
            return buf;
        }
        std::string PrettyStamp() {
            const std::time_t t = std::time(nullptr);
            std::tm tmv{};
#ifdef _WIN32
            localtime_s(&tmv, &t);
#else
            localtime_r(&t, &tmv);
#endif
            char buf[64];
            std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv);
            return buf;
        }
        float F32(const uint8_t* p) {
            float f;
            std::memcpy(&f, p, 4);
            return f;
        }
        std::string WorkDir() { return ReplayDirectory() + "/.recording"; }
    }

    ReplayController::Tap ReplayController::NewSessionTap() {
        const uint64_t gen = ++generation;
        cache.Reset();
        return [this, gen](int dir, uint32_t id, const std::vector<uint8_t>& body) {
            if (generation.load() != gen)
                return;
            if (dir == 0)
                cache.OnToClient(id, body);
            recorder.Push(dir ? Dir::ToServer : Dir::ToClient, id, body);
        };
    }

    void ReplayController::DetachSession() {
        generation++;
    }

    bool ReplayController::StartRecording(const LiveInfo& info, const std::string& perlinVersion, std::string& error) {
        if (recorder.Active())
            return false;
        Metadata m;
        m.name = "Replay " + PrettyStamp();
        m.server = info.server;
        m.gameVersion = info.gameVersion;
        m.playerName = info.playerName;
        m.perlinVersion = perlinVersion;
        m.protocol = info.protocol;
        m.createdUnix = static_cast<int64_t>(std::time(nullptr));
        m.localRuntimeId = info.runtimeId;
        m.startPose = info.pose;
        const std::vector<Record> snapshot = cache.Snapshot();
        return recorder.Start(WorkDir(), std::move(m), snapshot, error, [this] { return cache.Snapshot(); }, info.snapshotMs, 160u << 20);
    }

    bool ReplayController::StopRecording(std::string& outPath, std::string& error) {
        if (!recorder.Active()) {
            error = "not recording";
            return false;
        }
        outPath = ReplayDirectory() + "/" + Stamp() + kExtension;
        int n = 2;
        while (fs::exists(outPath)) {
            outPath = ReplayDirectory() + "/" + Stamp() + "_" + std::to_string(n++) + kExtension;
        }
        return recorder.Stop(outPath, {}, error);
    }

    bool ReplayController::RecoverInterrupted(std::string& recoveredPath) {
        // The recorder writes chunk_N.bin as it goes but metadata.json only on Stop(), so the folder alone is not enough to
        // rebuild timings. We rebuild the chunk table by scanning the files.
        const fs::path dir = WorkDir();
        std::error_code ec;
        if (!fs::exists(dir, ec))
            return false;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.path().filename().string().rfind("chunk_", 0) == 0)
                files.push_back(e.path());
        if (files.empty()) {
            fs::remove_all(dir, ec);
            return false;
        }
        std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
            auto num = [](const fs::path& p) { return std::atoi(p.filename().string().c_str() + 6); };
            return num(a) < num(b);
        });
        Metadata m;
        m.name = "Recovered replay " + PrettyStamp();
        m.createdUnix = static_cast<int64_t>(std::time(nullptr));
        std::ifstream pm(dir / "pending.json", std::ios::binary);
        if (pm) {
            std::string text((std::istreambuf_iterator<char>(pm)), std::istreambuf_iterator<char>());
            ui::Json j;
            std::string err;
            Metadata loaded;
            if (ui::Json::Parse(text, j, err) && Metadata::FromJson(j, loaded, err)) {
                m = loaded;
                m.name = "Recovered " + loaded.name;
            }
        }
        m.chunks.clear();
        uint32_t last = 0;
        for (const fs::path& f : files) {
            std::ifstream in(f, std::ios::binary);
            std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            ChunkInfo ci;
            ci.file = f.filename().string();
            size_t pos = 0;
            bool first = true;
            while (pos < raw.size()) {
                Record r;
                const size_t n = DecodeRecord(raw.data() + pos, raw.size() - pos, r);
                if (!n)
                    break;
                pos += n;
                if (first) {
                    ci.startMs = r.timeMs;
                    first = false;
                }
                ci.endMs = r.timeMs;
                ci.records++;
                last = std::max(last, r.timeMs);
            }
            if (ci.records)
                m.chunks.push_back(ci);
        }
        if (m.chunks.empty()) {
            fs::remove_all(dir, ec);
            return false;
        }
        m.durationMs = last;
        recoveredPath = ReplayDirectory() + "/recovered_" + Stamp() + kExtension;
        std::string error;
        const bool ok = PackArchive(dir.string(), m, {}, recoveredPath, error);
        if (ok)
            fs::remove_all(dir, ec);
        return ok;
    }

    bool ReplayController::Open(const std::string& p, std::string& error) {
        auto r = std::make_unique<Reader>();
        if (!r->Open(p, error))
            return false;
        reader = std::move(r);
        meta = reader->Meta();
        path = p;
        chunkRecords.clear();
        chunkLoaded = false;
        Rewind();
        return true;
    }

    void ReplayController::Close() {
        reader.reset();
        chunkRecords.clear();
        chunkLoaded = false;
        path.clear();
        meta = Metadata{};
        timeMs = 0;
        seekTarget = -1;
        ended = false;
    }

    void ReplayController::Rewind() {
        chunkIndex = 0;
        recordIndex = 0;
        chunkLoaded = false;
        chunkRecords.clear();
        timeMs = 0;
        ended = false;
        useSnapshot = false;
        recorded = meta.startPose;
    }

    void ReplayController::Seek(double ms) {
        ms = std::clamp(ms, 0.0, static_cast<double>(meta.durationMs));
        // Best snapshot chunk at or before the target (chunk 0 always works).
        size_t best = 0;
        for (size_t i = 0; i < meta.chunks.size(); i++)
            if (meta.chunks[i].snapshot && meta.chunks[i].startMs <= ms)
                best = i;
        const bool backwards = ms < timeMs - 1.0;
        const bool jumpAhead = best > chunkIndex + 1 && meta.chunks[best].startMs > timeMs;
        if (backwards || jumpAhead) {
            // World changes cannot be undone, so restart the replay session and start again from the snapshot.
            restartRequested = true;
            chunkIndex = best;
            recordIndex = 0;
            chunkLoaded = false;
            chunkRecords.clear();
            timeMs = best == 0 ? 0 : meta.chunks[best].startMs;
            useSnapshot = best != 0;
            recorded = meta.startPose;
        }
        seekTarget = ms;
        ended = false;
    }

    void ReplayController::LoadChunkFor(size_t index) {
        std::string error;
        chunkRecords.clear();
        chunkLoaded = reader && reader->LoadChunk(index, chunkRecords, error);
        recordIndex = 0;
    }

    bool ReplayController::Advance(double dtMs, const std::function<void(const Record&)>& inject, double budgetMs) {
        if (!reader)
            return false;
        const auto started = std::chrono::steady_clock::now();
        double target = timeMs;
        if (seekTarget >= 0)
            target = seekTarget;
        else if (!paused)
            target = timeMs + dtMs * speed;
        else
            return true;
        if (target >= meta.durationMs)
            target = meta.durationMs;
        for (;;) {
            if (!chunkLoaded) {
                if (chunkIndex >= meta.chunks.size()) {
                    ended = true;
                    break;
                }
                LoadChunkFor(chunkIndex);
                if (!chunkLoaded) {
                    ended = true;
                    break;
                }
            }
            bool progressed = false;
            while (recordIndex < chunkRecords.size()) {
                const Record& rec = chunkRecords[recordIndex];
                if (rec.timeMs > target)
                    break;
                if (rec.dir == Dir::Snapshot) {
                    if (useSnapshot)
                        inject(rec);
                } else if (rec.dir == Dir::ToServer) {
                    if (rec.id == 144 && rec.body.size() >= 20) {
                        recorded.pitch = F32(rec.body.data());
                        recorded.yaw = F32(rec.body.data() + 4);
                        recorded.x = F32(rec.body.data() + 8);
                        recorded.y = F32(rec.body.data() + 12);
                        recorded.z = F32(rec.body.data() + 16);
                        recorded.known = true;
                    }
                } else {
                    inject(rec);
                }
                recordIndex++;
                progressed = true;
                if (budgetMs > 0 && (recordIndex & 63) == 0 &&
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() > budgetMs) {
                    // out of time this frame: advance the clock only as far as we got
                    timeMs = std::min(target, static_cast<double>(rec.timeMs));
                    return true;
                }
            }
            if (recordIndex >= chunkRecords.size()) {
                useSnapshot = false;
                chunkIndex++;
                chunkLoaded = false;
                if (!progressed && chunkIndex >= meta.chunks.size())
                    break;
                continue;
            }
            break;
        }
        timeMs = target;
        if (seekTarget >= 0 && timeMs >= seekTarget - 0.5)
            seekTarget = -1;
        if (timeMs >= meta.durationMs && chunkIndex >= meta.chunks.size())
            ended = true;
        return !ended;
    }

    void ReplayController::AddPlaybackMarker(const std::string& name, uint32_t atMs) {
        Marker m;
        m.timeMs = atMs;
        m.name = name;
        meta.markers.push_back(m);
        std::sort(meta.markers.begin(), meta.markers.end(), [](const Marker& a, const Marker& b) { return a.timeMs < b.timeMs; });
    }

    bool ReplayController::SaveMarkers(std::string& error) {
        return SaveArchiveFile("metadata.json", meta.ToJson().Dump(2), error);
    }

    bool ReplayController::SaveArchiveFile(const std::string& name, const std::string& content, std::string& error) {
        if (path.empty())
            return false;
        // The reader keeps the archive open; release it while the file is rewritten.
        reader.reset();
        const bool ok = UpdateArchiveFile(path, name, content, error);
        auto r = std::make_unique<Reader>();
        std::string err2;
        if (r->Open(path, err2))
            reader = std::move(r);
        return ok;
    }
}

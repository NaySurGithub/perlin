// Replay export (video / image sequence / screenshot) and the keyframe editor panel.
#include "client/Perlin.hpp"

#include "client/input/Keys.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/ui/Json.hpp"
#include "client/ui/ReplayScreen.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace perlin {
    namespace fs = std::filesystem;

    namespace {
        using Clock = std::chrono::steady_clock;

        std::string ConfigPath() { return replay::ReplayDirectory() + "/export_settings.json"; }

        int Even(int v) { return std::max(2, v & ~1); }

        std::string Num(double v, int decimals = 0) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
            return buf;
        }
    }

    // ------------------------------------------------------------------ timeline persistence
    void Perlin::LoadTimeline() {
        timeline = replay::Timeline{};
        timelineDirty = false;
        const std::string text = replayCtl.ReadArchiveFile("editor.json");
        if (!text.empty() && !replay::Timeline::FromJson(text, timeline))
            std::cout << "[replay] editor.json could not be read; starting with an empty timeline" << std::endl;
    }

    void Perlin::SaveTimeline() {
        if (!timelineDirty)
            return;
        std::string error;
        const std::string json = timeline.Empty() ? std::string() : timeline.ToJson();
        if (!replayCtl.SaveArchiveFile("editor.json", json, error))
            std::cout << "[replay] could not save the timeline: " << error << std::endl;
        timelineDirty = false;
    }

    // ------------------------------------------------------------------ config persistence
    void Perlin::LoadExportConfig() {
        if (exportUi.cfgLoaded)
            return;
        exportUi.cfgLoaded = true;
        std::ifstream in(ConfigPath(), std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            exportUi.cfg = replay::ExportConfig::FromJson(ss.str());
        }
    }

    void Perlin::SaveExportConfig() {
        std::error_code ec;
        fs::create_directories(replay::ReplayDirectory(), ec);
        std::ofstream out(ConfigPath(), std::ios::binary | std::ios::trunc);
        if (out)
            out << exportUi.cfg.ToJson();
    }

    void Perlin::FramebufferSizeEffective(int& w, int& h) {
        if (exportJob.active) {
            w = exportJob.renderW;
            h = exportJob.renderH;
            return;
        }
        platform->GetFramebufferSize(w, h);
    }

    // ------------------------------------------------------------------ export job
    bool Perlin::ExportStart(const ExportRequest& req) {
        ExportJob& j = exportJob;
        if (j.active)
            return false;
        if (!replayCtl.Playing() || !session || !renderer) {
            exportUi.status = "Open a replay first";
            return false;
        }
        replay::ExportConfig cfg = req.cfg;
        const replay::ffmpeg::Container* cont = replay::ffmpeg::FindContainer(cfg.container);
        if (!cont) {
            exportUi.status = "Unknown format";
            return false;
        }
        const std::string replayName = replayCtl.Meta().name.empty() ? "replay" : replayCtl.Meta().name;
        j.cfg = cfg;
        j.screenshot = req.screenshot;
        j.error.clear();
        j.encoder.reset();
        j.useFfmpeg = false;
        j.imageSeq = false;
        const int ssaa = std::clamp(cfg.ssaa, 1, 4);
        int outW = std::clamp(cfg.width, 16, 16384), outH = std::clamp(cfg.height, 16, 16384);
        if (!req.screenshot && !cont->imageSequence) {
            outW = Even(outW);
            outH = Even(outH);
        }
        int renderW = outW * ssaa, renderH = outH * ssaa;
        // reduce supersampling when the render target would exceed what GPUs can handle
        while ((renderW > 16384 || renderH > 16384) && ssaa > 1)
            return ExportStart([&] { ExportRequest r = req; r.cfg.ssaa = ssaa - 1; return r; }());
        if (renderW > 16384 || renderH > 16384) {
            exportUi.status = "Resolution too large (max 16384)";
            return false;
        }
        j.outW = outW;
        j.outH = outH;
        j.renderW = renderW;
        j.renderH = renderH;
        j.name = replay::ExpandNameTemplate(cfg.nameTemplate, replayName, outW, outH, cfg.fps, cfg.container);
        const std::string dir = replay::ExportDirectory(cfg);
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            exportUi.status = "Cannot create " + dir;
            return false;
        }

        // ---- range ----
        const double dur = replayCtl.DurationMs();
        j.startMs = 0;
        j.endMs = dur;
        if (cfg.range == replay::RangeMode::InOut) {
            if (replayUi.inMs >= 0) j.startMs = replayUi.inMs;
            if (replayUi.outMs >= 0) j.endMs = replayUi.outMs;
        } else if (cfg.range == replay::RangeMode::Custom) {
            j.startMs = cfg.customStartSec * 1000.0;
            j.endMs = cfg.customEndSec * 1000.0;
        }
        j.startMs = std::clamp(j.startMs, 0.0, dur);
        j.endMs = std::clamp(j.endMs, 0.0, dur);
        if (req.screenshot) {
            j.startMs = j.endMs = replayCtl.TimeMs();
            j.total = 1;
        } else {
            if (j.endMs <= j.startMs + 1) {
                exportUi.status = "The selected range is empty";
                return false;
            }
            j.total = replay::CountExportFrames(timeline, j.startMs, j.endMs, cfg.fps);
            if (j.total < 1) {
                exportUi.status = "Nothing to export";
                return false;
            }
        }

        // ---- output ----
        if (req.screenshot) {
            j.outPath = replay::UniquePath(dir + "/" + j.name + ".png", false);
        } else {
            replay::ffmpeg::Settings& fs2 = j.fs;
            fs2 = replay::ffmpeg::Settings{};
            fs2.ffmpegPath = exportUi.cat.available ? exportUi.cat.path : std::string();
            fs2.container = cfg.container;
            fs2.codec = cfg.codec;
            fs2.encoderOverride = cfg.encoderOverride;
            fs2.muxerOverride = cfg.muxerOverride;
            fs2.width = outW;
            fs2.height = outH;
            fs2.inputWidth = renderW;
            fs2.inputHeight = renderH;
            fs2.fps = cfg.fps;
            fs2.crf = cfg.crf;
            fs2.bitrateKbps = cfg.bitrateKbps;
            fs2.preset = cfg.preset;
            fs2.pixelFormat = cfg.pixelFormat;
            fs2.extraArgs = cfg.extraArgs;
            fs2.gifColors = cfg.gifColors;
            const std::string ext = !cfg.extensionOverride.empty() ? cfg.extensionOverride : cont->ext;
            j.imageSeq = cont->imageSequence;
            if (j.imageSeq) {
                const std::string seqDir = replay::UniquePath(dir + "/" + j.name, true);
                fs::create_directories(seqDir, ec);
                j.outPath = seqDir;
                fs2.outputPath = seqDir + "/" + j.name + "_%05d." + ext;
            } else {
                j.outPath = replay::UniquePath(dir + "/" + j.name + "." + ext, false);
                fs2.outputPath = j.outPath;
            }
            const bool pngFallback = j.imageSeq && cfg.container == "png" && !exportUi.cat.available;
            if (pngFallback) {
                j.useFfmpeg = false; // written by miniz
            } else {
                if (!exportUi.cat.available) {
                    exportUi.status = "ffmpeg is needed for this format (Advanced tab), or pick PNG sequence";
                    return false;
                }
                j.encoder = std::make_unique<replay::ffmpeg::Encoder>();
                std::string error;
                if (!j.encoder->Start(fs2, exportUi.cat, error)) {
                    j.encoder.reset();
                    exportUi.status = error;
                    return false;
                }
                j.useFfmpeg = true;
            }
        }

        // ---- take over the replay ----
        j.savedHideHud = replayUi.hideHud;
        j.savedPaused = replayCtl.Paused();
        j.savedMenu = replayUi.menuOpen;
        j.savedSpeed = replayCtl.Speed();
        j.savedTimeMs = replayCtl.TimeMs();
        j.savedMode = replayCtl.cameraMode;
        replayUi.hideHud = cfg.hideGui;
        replayUi.menuOpen = false;
        replayUi.scrubbing = false;
        exportUi.open = false;
        kfUi.open = false;
        pauseShown = false;
        if (hud)
            hud->Overlay().ClearOverlay();
        input.SetTextMode(false);
        replayCtl.SetPaused(false);
        replayCtl.SetSpeed(1.0);
        renderer->SetForcedSize(renderW, renderH);
        j.stepMs = 1000.0 / cfg.fps;
        j.targetMs = j.startMs;
        j.frame = 0;
        j.virtualUs = 0;
        j.virtualBase = Clock::now();
        j.realStart = Clock::now();
        j.phaseStart = j.realStart;
        j.lastOverlay = {};
        j.settleIters = j.quietIters = j.waitIters = 0;
        j.capReady = false;
        j.advanceNow = false;
        j.finishing = false;
        j.phase = req.screenshot ? ExportJob::Phase::Settle : ExportJob::Phase::Seeking;
        if (!req.screenshot)
            replayCtl.Seek(j.startMs);
        j.active = true;
        std::cout << "[export] " << (req.screenshot ? "screenshot" : "video") << " " << outW << "x" << outH << " (render " << renderW << "x" << renderH << ") "
                  << cfg.fps << " fps, " << j.total << " frames -> " << j.outPath << std::endl;
        return true;
    }

    void Perlin::ExportFinish(bool cancelled, const std::string& error) {
        ExportJob& j = exportJob;
        if (!j.active)
            return;
        std::string message;
        std::string err = error;
        if (j.encoder) {
            std::string finishError;
            if (cancelled || !err.empty()) {
                // finalize what was written so a cancelled export still plays
                if (!j.encoder->Finish(finishError) && err.empty())
                    err = finishError;
            } else if (!j.encoder->Finish(finishError)) {
                err = finishError;
            }
            j.encoder.reset();
        }
        if (renderer)
            renderer->SetForcedSize(0, 0);
        j.active = false;
        replayUi.hideHud = j.savedHideHud;
        replayCtl.SetSpeed(j.savedSpeed);
        replayCtl.SetPaused(j.savedPaused);
        replayCtl.cameraMode = j.savedMode;
        replayUi.menuOpen = true;
        replayUi.openedAt = Clock::now();
        replayUi.signature.clear();
        input.SetTextMode(true);
        if (hud)
            hud->Overlay().ClearOverlay();
        const double secs = std::chrono::duration<double>(Clock::now() - j.realStart).count();
        if (!err.empty()) {
            message = "Export failed: " + err;
        } else if (cancelled) {
            message = "Export cancelled (" + std::to_string(j.frame) + " frames kept)";
        } else {
            message = (j.screenshot ? "Saved screenshot: " : "Export finished: ") + j.outPath + " (" + Num(secs, 1) + " s)";
        }
        std::cout << "[export] " << message << std::endl;
        exportUi.status = message;
        ReplayStatus(message);
        replayUi.statusUntil = Clock::now() + std::chrono::seconds(12);
        if (!cancelled && err.empty() && !exportUi.queue.empty())
            ExportStartNext();
        else if (cancelled)
            exportUi.queue.clear();
    }

    void Perlin::ExportStartNext() {
        while (!exportUi.queue.empty()) {
            ExportRequest r = exportUi.queue.front();
            exportUi.queue.erase(exportUi.queue.begin());
            if (ExportStart(r))
                return;
            std::cout << "[export] queued job skipped: " << exportUi.status << std::endl;
        }
    }

    void Perlin::ExportPreFrame(Clock::time_point& frameStart, float& dt) {
        ExportJob& j = exportJob;
        const auto realNow = Clock::now();
        if (!platform->isRunning) {
            ExportFinish(true, "");
            return;
        }
        if (inputManager.Pressed(Action::Pause)) {
            ExportFinish(true, "");
            return;
        }
        if (!replayCtl.Playing() || !session || !session->IsReplay()) {
            ExportFinish(true, "the replay was closed");
            return;
        }
        if (j.encoder && !j.encoder->Alive() && j.phase != ExportJob::Phase::Waiting) {
            ExportFinish(false, "ffmpeg stopped unexpectedly: " + j.encoder->Log().substr(0, 400));
            return;
        }
        j.advanceNow = false;
        switch (j.phase) {
        case ExportJob::Phase::Seeking:
            if (!replayCtl.SeekPending() && loadingDone && sessionReady) {
                j.phase = ExportJob::Phase::Settle;
                j.settleIters = j.quietIters = 0;
                j.phaseStart = realNow;
            } else if (realNow - j.phaseStart > std::chrono::seconds(120)) {
                ExportFinish(false, "timed out while seeking to the start");
                return;
            }
            break;
        case ExportJob::Phase::Settle: {
            j.settleIters++;
            const bool quiet = levelRenderer && levelRenderer->PendingMeshes() == 0 && levelRenderer->MeshesInFlight() == 0 && session->ChunkWorkIdle();
            j.quietIters = quiet ? j.quietIters + 1 : 0;
            if ((j.settleIters >= 3 && j.quietIters >= 3) || realNow - j.phaseStart > std::chrono::seconds(30))
                j.phase = ExportJob::Phase::Capture;
            break;
        }
        case ExportJob::Phase::Capture:
            break;
        case ExportJob::Phase::Waiting:
            if (j.capReady.load()) {
                // ---- consume the frame ----
                std::vector<uint8_t> frame;
                uint32_t w = 0, h = 0;
                {
                    std::lock_guard<std::mutex> lk(j.capMutex);
                    w = j.capW;
                    h = j.capH;
                    if (w == static_cast<uint32_t>(j.renderW) && h == static_cast<uint32_t>(j.renderH)) {
                        frame.resize(static_cast<size_t>(w) * h * 4);
                        for (uint32_t y = 0; y < h; y++) {
                            const uint8_t* src = j.capBuf.data() + static_cast<size_t>(j.capFlip ? h - 1 - y : y) * j.capPitch;
                            std::copy(src, src + static_cast<size_t>(w) * 4, frame.data() + static_cast<size_t>(y) * w * 4);
                        }
                    }
                }
                j.capReady = false;
                if (frame.empty()) {
                    // the back buffer had not been resized yet; try again
                    if (++j.waitIters > 120) {
                        ExportFinish(false, "the renderer would not produce " + std::to_string(j.renderW) + "x" + std::to_string(j.renderH) + " frames (got " + std::to_string(w) + "x" + std::to_string(h) + ")");
                        return;
                    }
                    j.phase = ExportJob::Phase::Capture;
                    break;
                }
                bool ok = true;
                std::string err;
                if (j.screenshot) {
                    std::vector<uint8_t> scaled;
                    const uint8_t* data = frame.data();
                    if (j.renderW != j.outW) {
                        scaled = replay::ffmpeg::DownscaleBgra(frame.data(), j.renderW, j.renderH, static_cast<size_t>(j.renderW) * 4, false, j.outW, j.outH);
                        data = scaled.data();
                    }
                    ok = replay::ffmpeg::WritePngBgra(j.outPath, j.outW, j.outH, data, static_cast<size_t>(j.outW) * 4, false, false);
                    if (!ok) err = "could not write " + j.outPath;
                } else if (j.useFfmpeg) {
                    ok = j.encoder->WriteFrame(frame.data(), frame.size());
                    if (!ok) err = "ffmpeg closed the pipe: " + j.encoder->Log().substr(0, 400);
                } else {
                    std::vector<uint8_t> scaled;
                    const uint8_t* data = frame.data();
                    if (j.renderW != j.outW) {
                        scaled = replay::ffmpeg::DownscaleBgra(frame.data(), j.renderW, j.renderH, static_cast<size_t>(j.renderW) * 4, false, j.outW, j.outH);
                        data = scaled.data();
                    }
                    char name[64];
                    std::snprintf(name, sizeof name, "_%05lld.png", j.frame + 1);
                    ok = replay::ffmpeg::WritePngBgra(j.outPath + "/" + j.name + name, j.outW, j.outH, data, static_cast<size_t>(j.outW) * 4, false, false);
                    if (!ok) err = "could not write frames to " + j.outPath;
                }
                if (!ok) {
                    ExportFinish(false, err);
                    return;
                }
                j.frame++;
                if (j.frame >= j.total) {
                    ExportFinish(false, "");
                    return;
                }
                j.targetMs += j.stepMs * timeline.SpeedAt(j.targetMs);
                j.phase = ExportJob::Phase::Advance;
            } else if (++j.waitIters > 600) {
                ExportFinish(false, "the renderer did not deliver a frame");
                return;
            }
            break;
        case ExportJob::Phase::Advance:
            break;
        }
        if (j.phase == ExportJob::Phase::Advance) {
            j.advanceNow = true;
            j.virtualUs += static_cast<long long>(1000000.0 / j.cfg.fps);
            j.phase = ExportJob::Phase::Settle;
            j.settleIters = j.quietIters = 0;
            j.phaseStart = realNow;
        }
        frameStart = j.virtualBase + std::chrono::microseconds(j.virtualUs);
        dt = j.advanceNow ? static_cast<float>(1.0 / j.cfg.fps) : 1e-5f;
    }

    void Perlin::ExportPostRender() {
        ExportJob& j = exportJob;
        if (!j.active || j.phase != ExportJob::Phase::Capture)
            return;
        ExportJob* job = &j;
        renderer->RequestFrameCapture([job](uint32_t w, uint32_t h, uint32_t pitch, const uint8_t* bgra, bool flipY) {
            std::lock_guard<std::mutex> lk(job->capMutex);
            job->capW = w;
            job->capH = h;
            job->capPitch = pitch;
            job->capFlip = flipY;
            job->capBuf.assign(bgra, bgra + static_cast<size_t>(pitch) * h);
            job->capReady = true;
        });
        j.phase = ExportJob::Phase::Waiting;
        j.waitIters = 0;
    }

    void Perlin::ExportOverlay(ui::Hud& h, int fbW, int fbH) {
        ExportJob& j = exportJob;
        const auto now = Clock::now();
        const bool show = j.phase == ExportJob::Phase::Settle && j.settleIters <= 1 && now - j.lastOverlay > std::chrono::milliseconds(500) && !j.screenshot;
        if (!show) {
            if (h.Overlay().HasOverlay() && !(j.phase == ExportJob::Phase::Settle && j.settleIters <= 1))
                h.Overlay().ClearOverlay();
            return;
        }
        j.lastOverlay = now;
        const double elapsed = std::chrono::duration<double>(now - j.realStart).count();
        const double perFrame = j.frame > 0 ? elapsed / static_cast<double>(j.frame) : 0.0;
        const double eta = perFrame * static_cast<double>(j.total - j.frame);
        ui::PanelModel m;
        m.title = "Exporting " + j.name;
        m.status = Num(100.0 * static_cast<double>(j.frame) / static_cast<double>(std::max<long long>(1, j.total)), 1) + "%";
        m.progress = static_cast<float>(j.frame) / static_cast<float>(std::max<long long>(1, j.total));
        const auto info = [&](const std::string& a, const std::string& b) {
            ui::PanelRow r;
            r.kind = ui::PanelRow::Kind::Info;
            r.label = a;
            r.value = b;
            m.rows.push_back(r);
        };
        info("Frame", std::to_string(j.frame) + " / " + std::to_string(j.total));
        info("Resolution", std::to_string(j.outW) + "x" + std::to_string(j.outH) + (j.renderW != j.outW ? " (supersampled " + std::to_string(j.renderW / j.outW) + "x)" : ""));
        info("Frame rate", replay::FpsLabel(j.cfg.fps));
        info("Replay time", ui::FormatReplayTime(j.targetMs) + " / " + ui::FormatReplayTime(j.endMs));
        info("Elapsed", ui::FormatReplayTime(elapsed * 1000.0));
        info("Remaining", j.frame > 2 ? ui::FormatReplayTime(eta * 1000.0) : std::string("estimating..."));
        info("Output", j.outPath);
        if (!exportUi.queue.empty())
            info("Queued after this", std::to_string(exportUi.queue.size()));
        ui::PanelRow esc;
        esc.kind = ui::PanelRow::Kind::Info;
        esc.label = "Press Esc to cancel";
        m.rows.push_back(esc);
        const int scale = h.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        std::string error;
        m.scroll = 0;
        if (!h.Overlay().SetOverlay(ui::BuildPanelRml(m, guiW, guiH), error))
            std::cout << "[ui] export overlay: " << error << std::endl;
        h.UpdateOverlay(fbW, fbH, options.guiScale);
    }
}

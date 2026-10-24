#include "client/ClientControl.hpp"

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
#include "client/input/KeyMapping.hpp"
#include "util/Json.hpp"
#include <httplib.h>
#include <stb_image_write.h>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <thread>

namespace perlin {
    struct ClientControl::Impl {
        httplib::Server server;
        std::thread thread;
        std::mutex mutex;
        std::vector<ControlCommand> commands;
        std::deque<std::string> traces;
        std::string state = "{}", frame;
        uint64_t nextId = 0;
        std::atomic<bool> frameRequested{false};
    };

    ClientControl::ClientControl() : impl(std::make_unique<Impl>()) {}
    ClientControl::~ClientControl() { Stop(); }

    bool ClientControl::Start(int port) {
        if (port < 1024 || port > 65535 || impl->thread.joinable()) return false;
        auto& p = *impl;
        p.server.set_payload_max_length(65536);
        p.server.set_pre_routing_handler([port](const httplib::Request& req, httplib::Response& res) {
            const auto host = req.get_header_value("Host");
            const auto origin = req.get_header_value("Origin");
            const std::string local = "127.0.0.1:" + std::to_string(port);
            const std::string localhost = "localhost:" + std::to_string(port);
            if ((host != local && host != localhost) ||
                (!origin.empty() && origin != "http://" + local && origin != "http://" + localhost)) {
                res.status = 403;
                return httplib::Server::HandlerResponse::Handled;
            }
            res.set_header("Cache-Control", "no-store");
            return httplib::Server::HandlerResponse::Unhandled;
        });
        p.server.Get("/state", [&p](const auto&, auto& res) {
            std::lock_guard lock(p.mutex);
            res.set_content(p.state, "application/json");
        });
        p.server.Get("/frame", [&p](const auto&, auto& res) {
            p.frameRequested = true;
            std::lock_guard lock(p.mutex);
            if (p.frame.empty()) { res.status = 202; return; }
            res.set_content(p.frame, "image/jpeg");
        });
        p.server.Get("/trace", [&p](const auto&, auto& res) {
            std::lock_guard lock(p.mutex);
            std::string body;
            for (const auto& line : p.traces) body += line + '\n';
            res.set_content(std::move(body), "application/x-ndjson");
        });
        p.server.Post("/command", [&p](const auto& req, auto& res) {
            if (req.get_header_value("Content-Type") != "application/json") { res.status = 415; return; }
            auto command = json::Read<ControlCommand>(req.body);
            if (!command) { res.status = 400; return; }
            auto& c = *command;
            bool valid = c.op == "input" || c.op == "look" || c.op == "command" || c.op == "chat" ||
                         c.op == "key" || c.op == "mouse" || c.op == "release" || c.op == "shutdown" || c.op == "probe";
            valid &= c.ticks >= 0 && c.ticks <= 72000 && std::isfinite(c.yaw) && std::isfinite(c.pitch) &&
                     std::abs(c.yaw) <= 36000 && std::abs(c.pitch) <= 90 &&
                     std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z) &&
                     std::abs(c.x) <= 30000000 && std::abs(c.y) <= 100000 && std::abs(c.z) <= 30000000 && c.key >= 0 && c.key < 256 &&
                     c.button >= 0 && c.button < 8 && c.text.size() <= 8192;
            for (const auto& name : c.actions) {
                bool found = false;
                for (size_t i = 0; i < kActionCount; ++i)
                    found |= name == ActionId(static_cast<Action>(i)) || "key." + name == ActionId(static_cast<Action>(i));
                valid &= found;
            }
            if (!valid) { res.status = 400; return; }
            std::lock_guard lock(p.mutex);
            if (p.commands.size() >= 128) { res.status = 429; return; }
            c.id = ++p.nextId;
            p.commands.push_back(c);
            res.status = 202;
            res.set_content("{\"id\":" + std::to_string(c.id) + "}", "application/json");
        });
        // Bind before starting the thread so startup failure is reported reliably.
        if (!p.server.bind_to_port("127.0.0.1", port)) return false;
        p.thread = std::thread([&p] { p.server.listen_after_bind(); });
        return true;
    }

    void ClientControl::Stop() {
        impl->server.stop();
        if (impl->thread.joinable()) impl->thread.join();
    }

    std::vector<ControlCommand> ClientControl::TakeCommands() {
        std::lock_guard lock(impl->mutex);
        std::vector<ControlCommand> out;
        out.swap(impl->commands);
        return out;
    }

    void ClientControl::Publish(const ControlSnapshot& state) {
        auto body = json::Write(state);
        std::lock_guard lock(impl->mutex);
        impl->state = std::move(body);
    }

    bool ClientControl::TakeFrameRequest() { return impl->frameRequested.exchange(false); }

    void ClientControl::Frame(uint32_t w, uint32_t h, uint32_t pitch, const uint8_t* bgra, bool flipY) {
        std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
        for (uint32_t y = 0; y < h; ++y) {
            const auto* row = bgra + static_cast<size_t>(flipY ? h - 1 - y : y) * pitch;
            for (uint32_t x = 0; x < w; ++x) {
                const size_t dst = (static_cast<size_t>(y) * w + x) * 3;
                rgb[dst] = row[x * 4 + 2]; rgb[dst + 1] = row[x * 4 + 1]; rgb[dst + 2] = row[x * 4];
            }
        }
        std::string jpeg;
        if (!stbi_write_jpg_to_func([](void* ctx, void* bytes, int n) {
                static_cast<std::string*>(ctx)->append(static_cast<char*>(bytes), n);
            }, &jpeg, w, h, 3, rgb.data(), 85)) return;
        std::lock_guard lock(impl->mutex);
        impl->frame = std::move(jpeg);
    }

    void ClientControl::Trace(const std::string& line) {
        std::lock_guard lock(impl->mutex);
        impl->traces.push_back(line);
        if (impl->traces.size() > 24000) impl->traces.pop_front();
    }
}
#else
namespace perlin {
    struct ClientControl::Impl {};
    ClientControl::ClientControl() : impl(std::make_unique<Impl>()) {}
    ClientControl::~ClientControl() = default;
    bool ClientControl::Start(int) { return false; }
    void ClientControl::Stop() {}
    std::vector<ControlCommand> ClientControl::TakeCommands() { return {}; }
    void ClientControl::Publish(const ControlSnapshot&) {}
    bool ClientControl::TakeFrameRequest() { return false; }
    void ClientControl::Frame(uint32_t, uint32_t, uint32_t, const uint8_t*, bool) {}
    void ClientControl::Trace(const std::string&) {}
}
#endif

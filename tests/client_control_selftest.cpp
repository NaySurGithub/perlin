#include "client/ClientControl.hpp"
#include "client/input/InputManager.hpp"
#include "util/Json.hpp"
#include <httplib.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <cstdio>

using namespace perlin;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (false)

int main() {
    Input input;
    Options options;
    InputManager manager(input, options);
    std::bitset<kActionCount> held;
    held.set(static_cast<size_t>(Action::Forward));
    held.set(static_cast<size_t>(Action::Jump));
    manager.SetControlTick(10);
    manager.SetControlActions(held, 12);
    manager.Poll();
    CHECK(manager.Pressed(Action::Jump));
    CHECK(manager.Axis(Action::Forward, Action::Back) == 1);
    manager.SetControlTick(11);
    CHECK(manager.IsDown(Action::Jump));
    manager.SetControlTick(12);
    CHECK(!manager.IsDown(Action::Jump));
    manager.SetControlActions(held, 14);
    manager.Poll();
    CHECK(manager.Pressed(Action::Jump)); // expired hold becomes a new press
    manager.SetServerLocks(512);
    CHECK(!manager.IsDown(Action::Forward));
    manager.SetServerLocks(0);
    manager.SetBlocked(true);
    manager.SetControlActions({}, 14);
    manager.SetControlActions(held, 14);
    manager.Poll();
    CHECK(!manager.Pressed(Action::Jump));
    CHECK(!manager.IsDown(Action::Jump));
    manager.SetBlocked(false);

    httplib::Server probe;
    const int port = probe.bind_to_any_port("127.0.0.1");
    CHECK(port >= 1024);
    probe.stop();
    ClientControl server;
    CHECK(server.Start(port));
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(2);
    client.set_read_timeout(2);
    auto result = client.Post("/command", R"({"op":"input","actions":["forward","jump"],"ticks":20})", "application/json");
    CHECK(result && result->status == 202);
    auto commands = server.TakeCommands();
    CHECK(commands.size() == 1 && commands[0].id == 1 && commands[0].ticks == 20);
    result = client.Post("/command", R"({"op":"input","actions":["unknown"]})", "application/json");
    CHECK(result && result->status == 400);
    result = client.Post("/command", R"({"op":"input","ticks":-1})", "application/json");
    CHECK(result && result->status == 400);
    result = client.Post("/command", R"({"op":"shutdown"})", "text/plain");
    CHECK(result && result->status == 415);
    result = client.Post("/command", {{"Origin", "https://unrelated.example"}}, R"({"op":"shutdown"})", "application/json");
    CHECK(result && result->status == 403);
    CHECK(server.TakeCommands().empty());
    ControlSnapshot snapshot;
    snapshot.tick = 41; snapshot.corrections = 3; snapshot.position = {1, 2, 3};
    server.Publish(snapshot);
    result = client.Get("/state");
    CHECK(result && result->status == 200);
    auto state = result ? json::Read<ControlSnapshot>(result->body) : std::nullopt;
    CHECK(state && state->tick == 41 && state->corrections == 3 && state->position[2] == 3);
    const uint8_t bgra[16] = {0,0,255,255, 0,0,255,255, 0,0,255,255, 0,0,255,255};
    server.Frame(2, 2, 8, bgra, false);
    result = client.Get("/frame");
    CHECK(result && result->status == 200);
    int w = 0, h = 0, n = 0;
    auto* pixels = result ? stbi_load_from_memory(reinterpret_cast<const unsigned char*>(result->body.data()),
                                                  static_cast<int>(result->body.size()), &w, &h, &n, 3) : nullptr;
    CHECK(pixels && w == 2 && h == 2 && pixels[0] > 200 && pixels[1] < 50 && pixels[2] < 50);
    stbi_image_free(pixels);
    CHECK(server.TakeFrameRequest());
    CHECK(!server.TakeFrameRequest());
    server.Trace("{\"tick\":41}");
    result = client.Get("/trace");
    CHECK(result && result->body == "{\"tick\":41}\n");
    server.Stop();
    std::printf("client control: %d failures\n", failures);
    return failures ? 1 : 0;
}

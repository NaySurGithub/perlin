#include "client/mods/ModManager.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

using namespace perlin::mods;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

template <typename F>
static bool WaitFor(F&& f, int ms = 6000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (f())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return f();
}

static ModInfo Find(const char* name) {
    for (const ModInfo& info : ModManager::Instance().List())
        if (info.name == name)
            return info;
    return {};
}

int main(int argc, char** argv) {
    if (argc < 2)
        return 2;
    const fs::path dir = fs::temp_directory_path() / "perlin_mod_selftest";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
    CHECK(!bytes.empty());
    auto write = [&](const char* name, const std::vector<unsigned char>& data) {
        std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    };
    write("speedometer.wasm", bytes);
    {
        std::vector<unsigned char> broken = bytes;
        broken.resize(20);
        write("broken.wasm", broken);
    }

    auto& mm = ModManager::Instance();
    mm.Start(dir.string());

    CHECK(WaitFor([&] { return Find("speedometer").state == "running"; }));
    CHECK(Find("speedometer").loads == 1);
    CHECK(WaitFor([&] { return Find("broken").state == "error"; }));

    HostState hs;
    hs.connected = true;
    hs.vx = 0.2;
    hs.x = 12;
    mm.SetHostState(hs);
    bool sawConnect = false, sawSpeed = false;
    CHECK(WaitFor(
        [&] {
            for (const Outbound& out : mm.TakeOutbound()) {
                if (out.text.find("connected") != std::string::npos)
                    sawConnect = true;
                if (out.text.rfind("speed 4.00", 0) == 0)
                    sawSpeed = true;
            }
            return sawConnect && sawSpeed;
        },
        8000));

    mm.SendChat("!pos");
    bool sawPos = false;
    CHECK(WaitFor(
        [&] {
            for (const Outbound& out : mm.TakeOutbound())
                if (out.text.rfind("x=12", 0) == 0)
                    sawPos = true;
            return sawPos;
        },
        4000));

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    {
        std::vector<unsigned char> grown = bytes;
        const unsigned char section[] = {0, 5, 4, 'p', 'a', 'd', '!'};
        grown.insert(grown.end(), std::begin(section), std::end(section));
        write("speedometer.wasm", grown);
    }
    CHECK(WaitFor([&] { return Find("speedometer").loads == 2; }));
    CHECK(Find("speedometer").state == "running");

    mm.SetDisabled({"speedometer"});
    CHECK(WaitFor([&] { return Find("speedometer").state == "disabled"; }));
    mm.SetDisabled({});
    CHECK(WaitFor([&] { return Find("speedometer").state == "running"; }));

    fs::remove(dir / "speedometer.wasm");
    CHECK(WaitFor([&] { return Find("speedometer").name.empty(); }));

    mm.Stop();
    fs::remove_all(dir);
    std::printf(failures ? "mod_selftest FAILED\n" : "mod_selftest ok\n");
    return failures ? 1 : 0;
}

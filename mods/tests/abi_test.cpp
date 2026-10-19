// Verifies the reflection-driven wire encoding against the original hand-written one, byte for byte.
#include <perlin/abi.hpp>
#include "/tmp/abi_old.hpp"
#include <cstdio>
#include <cstdlib>
#include <type_traits>
using namespace perlin;

static int counter = 1;
template <class T> void fill(T& v);
template <class T> void fill(T& v) {
    if constexpr (std::is_same_v<T, bool>) v = (counter++ % 2) == 0;
    else if constexpr (std::is_enum_v<T>) { v = static_cast<T>(counter++ % 3); }
    else if constexpr (abi::wire::Raw<T> && !std::is_arithmetic_v<T>) { v = T::from_bits(static_cast<decltype(v.bits())>(0x1234 + counter++)); }
    else if constexpr (std::is_arithmetic_v<T>) v = static_cast<T>(counter++);
    else if constexpr (std::is_same_v<T, std::string>) v = "s" + std::to_string(counter++);
    else if constexpr (abi::wire::is_array<T>::value) { for (auto& e : v) fill(e); }
    else if constexpr (abi::wire::is_vector<T>::value) { v.resize(2); for (auto& e : v) fill(e); }
    else if constexpr (abi::wire::is_pair<T>::value) { fill(v.first); fill(v.second); }
    else if constexpr (meta::Aggregate<T>) meta::for_each_field(v, [](auto& m) { fill(m); });
}
template <class New, class Old>
int check(const char* name) {
    New n{};
    fill(n);
    abi::Writer w1; w1.write(n);
    perlin_old::abi::Reader r(w1.Data(), w1.Size());
    Old o = Old::Read(r);
    if (!r.Ok() || r.Remaining() != 0) { std::printf("FAIL %s: old reader consumed wrongly (rem %zu)\n", name, r.Remaining()); return 1; }
    perlin_old::abi::Writer w2; o.Write(w2);
    if (w1.data != w2.data) { std::printf("FAIL %s: bytes differ (%zu vs %zu)\n", name, w1.Size(), w2.Size()); return 1; }
    // and back through the new reader
    abi::Reader r2(w2.Data(), w2.Size());
    New back = r2.read<New>();
    abi::Writer w3; w3.write(back);
    if (!r2.Ok() || r2.Remaining() != 0 || w3.data != w1.data) { std::printf("FAIL %s: new reader roundtrip\n", name); return 1; }
    std::printf("ok   %s (%zu bytes, %zu fields)\n", name, w1.Size(), meta::field_count<New>);
    return 0;
}
#define T(N) rc |= check<abi::N, perlin_old::abi::N>(#N);
int main() {
    int rc = 0;
    T(Item) T(PlayerState) T(Effect) T(Inventory) T(Entity) T(PlayerEntry) T(WorldState) T(BlockInfo) T(RayHit) T(AimState)
    T(ScreenState) T(Meta) T(InputOverrideState) T(Region) T(FoundBlock)
    T(EventTick) T(EventFrame) T(EventChat) T(EventKey) T(EventCommand) T(EventConnection) T(EventDamage) T(EventText)
    T(EventSlot) T(EventEntity) T(EventSound) T(EventHttp) T(EventPacket) T(EventTitle)
    rc |= check<abi::EventDimension, perlin_old::abi::EventChange>("EventChange");
    // truncated input must fail cleanly
    abi::Writer w; abi::Inventory inv; fill(inv); w.write(inv);
    abi::Reader cut(w.Data(), w.Size() / 2);
    (void)cut.read<abi::Inventory>();
    if (cut.Ok()) { std::puts("FAIL truncated read reported ok"); rc = 1; }
    // hostile length prefix must not allocate
    uint8_t evil[8] = {0xFF, 0xFF, 0xFF, 0x7F, 1, 2, 3, 4};
    abi::Reader hr(evil, 8);
    auto v = hr.read<std::vector<std::string>>();
    if (hr.Ok() || !v.empty()) { std::puts("FAIL hostile length"); rc = 1; }
    std::puts(rc ? "FAILED" : "all wire checks passed");
    return rc;
}

#include "util/Env.hpp"
#include "client/multiplayer/BedrockSession.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace perlin {
    using namespace proto;

    namespace {
        bool EndsWith(const std::string& s, const char* suffix) {
            const size_t n = std::char_traits<char>::length(suffix);
            return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
        }
        bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

        int MaxStack(const std::string& full) {
            const std::string n = full.rfind("minecraft:", 0) == 0 ? full.substr(10) : full;
            static const char* const kOne[] = {"_sword", "_pickaxe", "_axe", "_shovel", "_hoe", "_helmet", "_chestplate", "_leggings",
                                              "_boots", "_boat", "_raft", "_minecart", "_bed", "_shulker_box", "_horse_armor", "_harness",
                                              "_bucket", "potion", "_book", "_disc", "_banner_pattern"};
            for (const char* s : kOne)
                if (EndsWith(n, s) || (std::string(s) == "potion" && Has(n, "potion")))
                    return 1;
            static const char* const kOneNames[] = {"bow", "crossbow", "trident", "shield", "elytra", "fishing_rod", "shears", "flint_and_steel",
                                                    "carrot_on_a_stick", "warped_fungus_on_a_stick", "saddle", "totem_of_undying", "cake",
                                                    "mace", "brush", "spyglass", "minecart", "turtle_helmet", "wolf_armor", "shulker_box",
                                                    "writable_book", "written_book", "enchanted_book", "boat", "lead_cake", "bundle"};
            for (const char* s : kOneNames)
                if (n == s)
                    return 1;
            static const char* const k16[] = {"ender_pearl", "snowball", "egg", "bucket", "honey_bottle", "wind_charge", "armor_stand"};
            for (const char* s : k16)
                if (n == s)
                    return 16;
            if (EndsWith(n, "_sign") || EndsWith(n, "_hanging_sign") || EndsWith(n, "_banner"))
                return 16;
            return 64;
        }

        int ArmorSlotOf(const std::string& full) {
            const std::string n = full.rfind("minecraft:", 0) == 0 ? full.substr(10) : full;
            if (EndsWith(n, "_helmet") || n == "carved_pumpkin" || EndsWith(n, "_head") || EndsWith(n, "_skull"))
                return 0;
            if (EndsWith(n, "_chestplate") || n == "elytra")
                return 1;
            if (EndsWith(n, "_leggings"))
                return 2;
            if (EndsWith(n, "_boots"))
                return 3;
            return -1;
        }

        const std::vector<uint8_t>* NbtOf(const ItemV4& item) {
            if (const auto* with = std::get_if<ItemExtraDataWithBlockingTick>(&item.extra))
                return with->nbt ? &with->nbt->nbt.bytes : nullptr;
            if (const auto* without = std::get_if<ItemExtraDataWithoutBlockingTick>(&item.extra))
                return without->nbt ? &without->nbt->nbt.bytes : nullptr;
            return nullptr;
        }

        bool SameStack(const ItemV4& a, const ItemV4& b) {
            if (a.networkId != b.networkId || a.metadata != b.metadata || a.blockRuntimeId != b.blockRuntimeId)
                return false;
            const auto* na = NbtOf(a);
            const auto* nb = NbtOf(b);
            if (!na && !nb)
                return true;
            if (!na || !nb)
                return false;
            return *na == *nb;
        }

        bool TagMatches(const std::string& tag, const std::string& full) {
            const std::string t = tag.rfind("minecraft:", 0) == 0 ? tag.substr(10) : tag;
            const std::string n = full.rfind("minecraft:", 0) == 0 ? full.substr(10) : full;
            if (t == "planks") return EndsWith(n, "_planks");
            if (t == "logs" || t == "logs_that_burn" || Has(t, "_logs"))
                return Has(n, "_log") || EndsWith(n, "_stem") || EndsWith(n, "_wood") || EndsWith(n, "_hyphae");
            if (t == "coals") return n == "coal" || n == "charcoal";
            if (t == "stone_crafting_materials" || t == "stone_tool_materials")
                return n == "cobblestone" || n == "blackstone" || n == "cobbled_deepslate";
            if (t == "soul_fire_base_blocks") return n == "soul_sand" || n == "soul_soil";
            if (t == "wool") return EndsWith(n, "_wool");
            if (t == "wooden_slabs") return EndsWith(n, "_slab") && Has(n, "planks") == false && !Has(n, "stone") && !Has(n, "brick");
            std::string base = t;
            if (!base.empty() && base.back() == 's')
                base.pop_back();
            return !base.empty() && Has(n, base.c_str());
        }

        void Address(StackRequestSlotInfo& s, int a, const ItemV4& raw) {
            using C = ContainerSlotType;
            C type = C::HotbarAndInventory;
            int slot = a;
            if (a >= BedrockSession::kInvArmor0 && a < BedrockSession::kInvOffhand) { type = C::Armor; slot = a - BedrockSession::kInvArmor0; }
            else if (a == BedrockSession::kInvOffhand) { type = C::Offhand; slot = 0; }
            else if (a >= BedrockSession::kInvCraft0 && a < BedrockSession::kInvOutput) { type = C::CraftingInput; slot = 28 + a - BedrockSession::kInvCraft0; }
            else if (a == BedrockSession::kInvOutput) { type = C::CraftingOutput; slot = 50; }
            else if (a == BedrockSession::kInvCursor) { type = C::Cursor; slot = 0; }
            s.slotType.containerId = type;
            s.slotType.dynamicContainerId.reset();
            s.slot = static_cast<uint8_t>(slot);
            s.stackId = raw.stackId.value_or(0);
        }
    }

    BedrockSession::ItemStackInfo& BedrockSession::InvInfoLocked(int a) {
        if (a < 9) return inventory.hotbar[a];
        if (a < 36) return inventory.main[a - 9];
        if (a < 40) return inventory.armor[a - 36];
        if (a == kInvOffhand) return inventory.offhand;
        if (a < kInvOutput) return inventory.craft[a - kInvCraft0];
        if (a == kInvOutput) return inventory.output;
        return inventory.cursor;
    }

    ItemV4& BedrockSession::InvRawLocked(int a) { return a < 36 ? inventoryRaw[a] : extraRaw[a - 36]; }

    namespace {
        struct Ing {
            bool empty = true;
            std::string name, tag;
            int meta = 0;
        };
        bool IngMatches(const Ing& ing, const BedrockSession::ItemStackInfo& it) {
            if (ing.empty)
                return it.Empty();
            if (it.Empty())
                return false;
            if (ing.name.empty() && !ing.tag.empty())
                return TagMatches(ing.tag, it.name);
            if (ing.name != it.name)
                return false;
            return ing.meta == 0x7fff || ing.meta < 0 || ing.meta == it.aux || it.aux == 0;
        }
    }

    void BedrockSession::InvRecomputeOutputLocked() {
        craftedRecipe = -1;
        inventory.output = ItemStackInfo{};
        extraRaw[kInvOutput - 36] = ItemV4{};
        int filled = 0;
        for (const auto& c : inventory.craft)
            filled += c.Empty() ? 0 : 1;
        if (filled == 0)
            return;
        for (size_t ri = 0; ri < recipes.size(); ri++) {
            const RecipeInfo& r = recipes[ri];
            bool ok = false;
            if (r.shaped) {
                int x0 = 2, y0 = 2, x1 = -1, y1 = -1;
                for (int i = 0; i < 4; i++)
                    if (!inventory.craft[i].Empty()) {
                        x0 = std::min(x0, i % 2); x1 = std::max(x1, i % 2);
                        y0 = std::min(y0, i / 2); y1 = std::max(y1, i / 2);
                    }
                if (x1 - x0 + 1 != r.w || y1 - y0 + 1 != r.h)
                    continue;
                for (int mirror = 0; mirror < 2 && !ok; mirror++) {
                    bool good = true;
                    for (int y = 0; y < r.h && good; y++)
                        for (int x = 0; x < r.w && good; x++) {
                            const size_t idx = static_cast<size_t>(y * r.w + (mirror ? r.w - 1 - x : x));
                            if (idx >= r.in.size()) { good = false; break; }
                            const auto& src = r.in[idx];
                            Ing ing{src.empty, src.name, src.tag, src.meta};
                            good = IngMatches(ing, inventory.craft[(y0 + y) * 2 + (x0 + x)]);
                        }
                    ok = good;
                }
            } else {
                std::vector<Ing> need;
                for (const auto& i : r.in)
                    if (!i.empty)
                        need.push_back(Ing{i.empty, i.name, i.tag, i.meta});
                if (static_cast<int>(need.size()) != filled)
                    continue;
                std::array<bool, 4> used{};
                std::function<bool(size_t)> place = [&](size_t k) {
                    if (k == need.size())
                        return true;
                    for (int c = 0; c < 4; c++) {
                        if (used[static_cast<size_t>(c)] || inventory.craft[c].Empty() || !IngMatches(need[k], inventory.craft[c]))
                            continue;
                        used[static_cast<size_t>(c)] = true;
                        if (place(k + 1))
                            return true;
                        used[static_cast<size_t>(c)] = false;
                    }
                    return false;
                };
                ok = place(0);
            }
            if (ok) {
                craftedRecipe = static_cast<int>(ri);
                inventory.output = r.outInfo;
                extraRaw[kInvOutput - 36] = r.out;
                return;
            }
        }
    }

    bool BedrockSession::InvBusy() const {
        std::lock_guard lock(statusMutex);
        return invPendingRequest != 0 || !invQueue.empty();
    }

    void BedrockSession::InvEnqueue(std::function<bool(ItemStackRequest&)> op) {
        {
            std::lock_guard lock(statusMutex);
            invQueue.push_back(std::move(op));
        }
        InvPump();
    }

    void BedrockSession::InvPump() {
        for (;;) {
            ItemStackRequestPacket pkt;
            bool send = false, close = false;
            {
                std::lock_guard lock(statusMutex);
                if (invPendingRequest != 0 && std::chrono::steady_clock::now() - invPendingAt < std::chrono::milliseconds(1500))
                    return;
                invPendingRequest = 0;
                if (invQueue.empty()) {
                    if (!invCloseWanted)
                        return;
                    invCloseWanted = false;
                    close = true;
                } else {
                    auto op = std::move(invQueue.front());
                    invQueue.pop_front();
                    ItemStackRequest r;
                    r.requestId = nextStackRequestId;
                    nextStackRequestId -= 2;
                    r.cause = static_cast<ItemStackRequestCause>(-1);
                    if (op(r) && !r.actions.empty()) {
                        if (PERLIN_ENV("PERLIN_INV_DEBUG")) {
                            std::cout << "[inventory] send request " << r.requestId << ":";
                            for (const auto& a : r.actions)
                                std::cout << ' ' << static_cast<int>(a.typeId) << '(' << static_cast<int>(a.count) << ',' << static_cast<int>(a.source.slotType.containerId) << ':' << static_cast<int>(a.source.slot) << '#' << a.source.stackId << "->" << static_cast<int>(a.destination.slotType.containerId) << ':' << static_cast<int>(a.destination.slot) << ')';
                            std::cout << std::endl;
                        }
                        invPendingRequest = r.requestId;
                        invPendingAt = std::chrono::steady_clock::now();
                        pkt.requests.push_back(std::move(r));
                        send = true;
                    }
                }
            }
            if (close) {
                SendCloseNow();
                return;
            }
            if (send) {
                Send(pkt);
                return;
            }
        }
    }

    namespace {
        struct Mover {
            BedrockSession& s;
            ItemStackRequest& r;
            std::function<BedrockSession::ItemStackInfo&(int)> info;
            std::function<ItemV4&(int)> raw;

            void Move(int src, int dst, int n) {
                auto& si = info(src);
                auto& di = info(dst);
                auto& sr = raw(src);
                auto& dr = raw(dst);
                ItemStackRequestActionsEntry a;
                const ItemStackRequestActionsEntryTypeId type = dst == BedrockSession::kInvCursor ? ItemStackRequestActionsEntryTypeId::Take
                                                                                                  : ItemStackRequestActionsEntryTypeId::Place;
                a.typeId = type;
                a.legacyTypeId = static_cast<uint8_t>(type);
                a.count = static_cast<uint8_t>(n);
                Address(a.source, src, sr);
                Address(a.destination, dst, dr);
                r.actions.push_back(a);
                if (di.Empty()) {
                    di = si;
                    dr = sr;
                    di.count = n;
                    dr.count = static_cast<uint16_t>(n);
                    if (n != si.count) {
                        dr.stackId.reset();
                        dr.hasStackId = false;
                    }
                } else {
                    di.count += n;
                    dr.count = static_cast<uint16_t>(dr.count + n);
                }
                si.count -= n;
                sr.count = static_cast<uint16_t>(sr.count - n);
                if (si.count <= 0) {
                    si = BedrockSession::ItemStackInfo{};
                    sr = ItemV4{};
                }
            }

            void Swap(int a, int b) {
                ItemStackRequestActionsEntry act;
                act.typeId = ItemStackRequestActionsEntryTypeId::Swap;
                act.legacyTypeId = static_cast<uint8_t>(act.typeId);
                Address(act.source, a, raw(a));
                Address(act.destination, b, raw(b));
                r.actions.push_back(act);
                std::swap(info(a), info(b));
                std::swap(raw(a), raw(b));
            }
        };
    }

    static bool QuickMove(Mover& m, int src, const std::vector<std::pair<int, int>>& ranges) {
        auto& si = m.info(src);
        if (si.Empty())
            return false;
        const int max = MaxStack(si.name);
        bool moved = false;
        for (int pass = 0; pass < 2 && !si.Empty(); pass++)
            for (const auto& [from, to] : ranges)
                for (int d = from; d <= to && !si.Empty(); d++) {
                    if (d == src)
                        continue;
                    auto& di = m.info(d);
                    if (pass == 0) {
                        if (di.Empty() || !SameStack(m.raw(d), m.raw(src)) || di.count >= max)
                            continue;
                        m.Move(src, d, std::min(si.count, max - di.count));
                        moved = true;
                    } else if (di.Empty()) {
                        m.Move(src, d, std::min(si.count, max));
                        moved = true;
                    }
                }
        return moved;
    }

    static void CraftPrelude(Mover& m, BedrockSession::ItemStackInfo outInfo, const ItemV4& outRaw, uint32_t recipeId) {
        ItemStackRequestActionsEntry craft;
        craft.typeId = ItemStackRequestActionsEntryTypeId::CraftRecipe;
        craft.legacyTypeId = static_cast<uint8_t>(craft.typeId);
        craft.recipeNetworkId = recipeId;
        craft.timesCrafted = 1;
        m.r.actions.push_back(craft);
        ItemStackRequestActionsEntry res;
        res.typeId = ItemStackRequestActionsEntryTypeId::ResultsDeprecated;
        res.legacyTypeId = static_cast<uint8_t>(res.typeId);
        ItemStackRequestInstanceDescriptor d;
        d.type = ItemStackRequestInstanceDescriptorType::Name;
        d.legacyType = 1;
        d.name = outInfo.name;
        d.metadata = static_cast<int32_t>(outRaw.metadata);
        d.count = static_cast<int16_t>(outInfo.count);
        d.blockRuntimeId = outRaw.blockRuntimeId;
        res.resultItems.push_back(d);
        res.timesCrafted = 1;
        m.r.actions.push_back(res);
        for (int i = 0; i < 4; i++) {
            const int a = BedrockSession::kInvCraft0 + i;
            if (m.info(a).Empty())
                continue;
            ItemStackRequestActionsEntry c;
            c.typeId = ItemStackRequestActionsEntryTypeId::Consume;
            c.legacyTypeId = static_cast<uint8_t>(c.typeId);
            c.count = 1;
            Address(c.source, a, m.raw(a));
            m.r.actions.push_back(c);
            auto& inf = m.info(a);
            auto& rw = m.raw(a);
            inf.count -= 1;
            rw.count = static_cast<uint16_t>(rw.count - 1);
            if (inf.count <= 0) {
                inf = BedrockSession::ItemStackInfo{};
                rw = ItemV4{};
            }
        }
    }

    void BedrockSession::InvClick(int addr, bool right) {
        if (addr < 0 || addr >= kInvCursor)
            return;
        InvEnqueue([this, addr, right](ItemStackRequest& r) {
            Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); }, [this](int a) -> ItemV4& { return InvRawLocked(a); }};
            auto& S = InvInfoLocked(addr);
            auto& C = InvInfoLocked(kInvCursor);
            const ItemV4& sRaw = InvRawLocked(addr);
            const ItemV4& cRaw = InvRawLocked(kInvCursor);
            if (addr == kInvOutput) {
                if (S.Empty() || craftedRecipe < 0 || craftedRecipe >= static_cast<int>(recipes.size()))
                    return false;
                if (!C.Empty() && (!SameStack(cRaw, sRaw) || C.count + S.count > MaxStack(C.name)))
                    return false;
                const ItemStackInfo outInfo = S;
                const ItemV4 outRaw = sRaw;
                CraftPrelude(m, outInfo, outRaw, recipes[static_cast<size_t>(craftedRecipe)].netId);
                inventory.output = outInfo;
                extraRaw[kInvOutput - 36] = outRaw;
                extraRaw[kInvOutput - 36].stackId = r.requestId;
                extraRaw[kInvOutput - 36].hasStackId = true;
                m.Move(kInvOutput, kInvCursor, outInfo.count);
                InvRecomputeOutputLocked();
                return true;
            }
            const bool cEmpty = C.Empty(), sEmpty = S.Empty();
            if (!cEmpty) {
                if (addr >= kInvArmor0 && addr < kInvOffhand && ArmorSlotOf(C.name) != addr - kInvArmor0)
                    return false;
            }
            const int max = cEmpty ? 64 : MaxStack(C.name);
            if (!right) {
                if (cEmpty) {
                    if (sEmpty) return false;
                    m.Move(addr, kInvCursor, S.count);
                } else if (sEmpty) {
                    m.Move(kInvCursor, addr, C.count);
                } else if (SameStack(sRaw, cRaw)) {
                    const int n = std::min(C.count, max - S.count);
                    if (n <= 0) return false;
                    m.Move(kInvCursor, addr, n);
                } else {
                    m.Swap(kInvCursor, addr);
                }
            } else {
                if (cEmpty) {
                    if (sEmpty) return false;
                    m.Move(addr, kInvCursor, (S.count + 1) / 2);
                } else if (sEmpty) {
                    m.Move(kInvCursor, addr, 1);
                } else if (SameStack(sRaw, cRaw)) {
                    if (S.count >= max) return false;
                    m.Move(kInvCursor, addr, 1);
                } else {
                    m.Swap(kInvCursor, addr);
                }
            }
            if (addr >= kInvCraft0 && addr < kInvOutput)
                InvRecomputeOutputLocked();
            return !r.actions.empty();
        });
    }

    void BedrockSession::InvShiftClick(int addr) {
        if (addr < 0 || addr >= kInvCursor)
            return;
        InvEnqueue([this, addr](ItemStackRequest& r) {
            Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); }, [this](int a) -> ItemV4& { return InvRawLocked(a); }};
            auto& S = InvInfoLocked(addr);
            if (S.Empty())
                return false;
            const std::vector<std::pair<int, int>> mainThenHotbar = {{9, 35}, {0, 8}};
            bool ok = false;
            if (addr == kInvOutput) {
                if (craftedRecipe < 0 || craftedRecipe >= static_cast<int>(recipes.size()))
                    return false;
                const ItemStackInfo outInfo = S;
                ItemV4 outRaw = InvRawLocked(addr);
                CraftPrelude(m, outInfo, outRaw, recipes[static_cast<size_t>(craftedRecipe)].netId);
                inventory.output = outInfo;
                extraRaw[kInvOutput - 36] = outRaw;
                extraRaw[kInvOutput - 36].stackId = r.requestId;
                extraRaw[kInvOutput - 36].hasStackId = true;
                ok = QuickMove(m, kInvOutput, {{0, 8}, {9, 35}});
                inventory.output = ItemStackInfo{};
                extraRaw[kInvOutput - 36] = ItemV4{};
                InvRecomputeOutputLocked();
                return ok;
            }
            if (addr < 36) {
                const int slot = ArmorSlotOf(S.name);
                if (slot >= 0 && InvInfoLocked(kInvArmor0 + slot).Empty())
                    ok = QuickMove(m, addr, {{kInvArmor0 + slot, kInvArmor0 + slot}});
                if (!ok)
                    ok = QuickMove(m, addr, addr < 9 ? std::vector<std::pair<int, int>>{{9, 35}} : std::vector<std::pair<int, int>>{{0, 8}});
            } else {
                ok = QuickMove(m, addr, mainThenHotbar);
            }
            if (addr >= kInvCraft0 && addr < kInvOutput)
                InvRecomputeOutputLocked();
            return ok;
        });
    }

    void BedrockSession::InvHotbarKey(int addr, int hotbar) {
        if (addr < 0 || addr >= 36 || hotbar < 0 || hotbar > 8 || addr == hotbar)
            return;
        InvEnqueue([this, addr, hotbar](ItemStackRequest& r) {
            Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); }, [this](int a) -> ItemV4& { return InvRawLocked(a); }};
            if (InvInfoLocked(addr).Empty() && InvInfoLocked(hotbar).Empty())
                return false;
            m.Swap(hotbar, addr);
            return true;
        });
    }

    void BedrockSession::InvDropCursor(bool whole) {
        InvEnqueue([this, whole](ItemStackRequest& r) {
            auto& C = InvInfoLocked(kInvCursor);
            auto& raw = InvRawLocked(kInvCursor);
            if (C.Empty())
                return false;
            ItemStackRequestActionsEntry a;
            a.typeId = ItemStackRequestActionsEntryTypeId::Drop;
            a.legacyTypeId = static_cast<uint8_t>(a.typeId);
            a.count = static_cast<uint8_t>(whole ? std::min(C.count, 255) : 1);
            Address(a.source, kInvCursor, raw);
            r.actions.push_back(a);
            C.count -= a.count;
            raw.count = static_cast<uint16_t>(raw.count - a.count);
            if (C.count <= 0) {
                C = ItemStackInfo{};
                raw = ItemV4{};
            }
            return true;
        });
    }

    void BedrockSession::InvDropSlot(int addr, bool whole) {
        if (addr < 0 || addr >= 36)
            return;
        DropInventorySlot(addr, whole);
    }

    void BedrockSession::InvReturnCursor() {
        for (int src : {kInvCursor, kInvCraft0, kInvCraft0 + 1, kInvCraft0 + 2, kInvCraft0 + 3}) {
            InvEnqueue([this, src](ItemStackRequest& r) {
                Mover m{*this, r, [this](int a) -> ItemStackInfo& { return InvInfoLocked(a); }, [this](int a) -> ItemV4& { return InvRawLocked(a); }};
                const bool ok = QuickMove(m, src, {{0, 8}, {9, 35}});
                if (src >= kInvCraft0)
                    InvRecomputeOutputLocked();
                return ok;
            });
        }
    }
}

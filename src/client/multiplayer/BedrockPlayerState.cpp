#include <array>
#include "util/Env.hpp"
#include <cstdlib>
#include "client/multiplayer/BedrockSession.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace perlin {
    using namespace proto;

    namespace {
        bool FindNbtInt(const std::vector<uint8_t>& nbt, const char* name, int& out) {
            const size_t n = std::strlen(name);
            for (size_t i = 0; i + n + 3 <= nbt.size(); i++) {
                if (nbt[i] != 0x03)
                    continue;
                if (i + 3 + n + 4 <= nbt.size() && nbt[i + 1] == n && nbt[i + 2] == 0 &&
                    std::memcmp(&nbt[i + 3], name, n) == 0) {
                    int32_t v;
                    std::memcpy(&v, &nbt[i + 3 + n], 4);
                    out = v;
                    return true;
                }
                if (i + 2 + n < nbt.size() && nbt[i + 1] == n && std::memcmp(&nbt[i + 2], name, n) == 0) {
                    uint32_t raw = 0;
                    int shift = 0;
                    size_t p = i + 2 + n;
                    while (p < nbt.size() && shift < 35) {
                        const uint8_t b = nbt[p++];
                        raw |= static_cast<uint32_t>(b & 0x7f) << shift;
                        if (!(b & 0x80))
                            break;
                        shift += 7;
                    }
                    out = static_cast<int>((raw >> 1) ^ (~(raw & 1) + 1));
                    return true;
                }
            }
            return false;
        }

        bool FindNbtString(const std::vector<uint8_t>& nbt, const char* name, std::string& out) {
            const size_t n = std::strlen(name);
            for (size_t i = 0; i + n + 4 <= nbt.size(); i++) {
                if (nbt[i] != 0x08)
                    continue;
                size_t p = 0;
                if (nbt[i + 1] == n && nbt[i + 2] == 0 && std::memcmp(&nbt[i + 3], name, n) == 0)
                    p = i + 3 + n;
                else if (nbt[i + 1] == n && std::memcmp(&nbt[i + 2], name, n) == 0)
                    p = i + 2 + n;
                else
                    continue;
                size_t len = 0;
                if (p + 2 <= nbt.size() && nbt[i + 2] == 0 && nbt[i + 1] == n) {
                    len = nbt[p] | (static_cast<size_t>(nbt[p + 1]) << 8);
                    p += 2;
                } else {
                    int shift = 0;
                    while (p < nbt.size() && shift < 35) {
                        const uint8_t b = nbt[p++];
                        len |= static_cast<size_t>(b & 0x7f) << shift;
                        if (!(b & 0x80))
                            break;
                        shift += 7;
                    }
                }
                if (len == 0 || p + len > nbt.size() || len > 256)
                    continue;
                out.assign(reinterpret_cast<const char*>(&nbt[p]), len);
                return true;
            }
            return false;
        }

        bool Contains(const std::vector<uint8_t>& bytes, const char* text) {
            const size_t n = std::strlen(text);
            if (bytes.size() < n)
                return false;
            return std::search(bytes.begin(), bytes.end(), text, text + n) != bytes.end();
        }

        const std::vector<uint8_t>* ItemNbt(const ItemV4& item) {
            if (const auto* with = std::get_if<ItemExtraDataWithBlockingTick>(&item.extra))
                return with->nbt ? &with->nbt->nbt.bytes : nullptr;
            if (const auto* without = std::get_if<ItemExtraDataWithoutBlockingTick>(&item.extra))
                return without->nbt ? &without->nbt->nbt.bytes : nullptr;
            return nullptr;
        }

        bool IntegerOf(const MetadataDictionaryEntry& e, int& out) {
            bool found = false;
            std::visit(
                [&](const auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, int64_t>) {
                        out = static_cast<int>(v);
                        found = true;
                    } else if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, float>) {
                    } else {
                        std::visit(
                            [&](const auto& inner) {
                                using U = std::decay_t<decltype(inner)>;
                                if constexpr (std::is_same_v<U, int8_t> || std::is_same_v<U, int16_t> ||
                                              std::is_same_v<U, int32_t> || std::is_same_v<U, int64_t>) {
                                    out = static_cast<int>(inner);
                                    found = true;
                                }
                            },
                            v);
                    }
                },
                e.value);
            return found;
        }
    }

    void BedrockSession::ResetPlayerState() {
        std::lock_guard lock(statusMutex);
        vitals = Vitals{};
        inventory = InventoryView{};
        for (ItemV4& raw : inventoryRaw)
            raw = ItemV4{};
        for (ItemV4& raw : extraRaw)
            raw = ItemV4{};
        invQueue.clear();
        ridingUid = 0;
        invPendingRequest = 0;
        recipes.clear();
        craftedRecipe = -1;
        titleText = TitleText{};
    }

    BedrockSession::Vitals BedrockSession::GetVitals() const {
        std::lock_guard lock(statusMutex);
        return vitals;
    }

    BedrockSession::InventoryView BedrockSession::GetInventory() const {
        std::lock_guard lock(statusMutex);
        return inventory;
    }

    BedrockSession::TitleText BedrockSession::GetTitleText() const {
        std::lock_guard lock(statusMutex);
        return titleText;
    }

    bool BedrockSession::SelectHotbarSlot(int slot) {
        if (slot < 0 || slot > 8)
            return false;
        MobEquipmentPacket p;
        {
            std::lock_guard lock(statusMutex);
            inventory.selected = slot;
            p.item = inventoryRaw[slot];
        }
        p.runtimeEntityId = runtimeId;
        p.slot = static_cast<uint8_t>(slot);
        p.selectedSlot = static_cast<uint8_t>(slot);
        p.windowId = WindowID::Inventory;
        return Send(p);
    }

    bool BedrockSession::SendSwingArm(const char* source) {
        AnimatePacket p;
        p.actionId = AnimatePacketActionId::SwingArm;
        p.runtimeEntityId = runtimeId;
        p.hasSwingSource = true;
        p.swingSource = source;
        return Send(p);
    }

    void BedrockSession::RegisterPlayerStateHandlers() {
        const auto convert = [this](const ItemV4& raw) {
            ItemStackInfo info;
            if (raw.networkId == 0 || raw.count == 0)
                return info;
            auto it = itemNames.find(raw.networkId);
            info.name = it == itemNames.end() ? std::string("minecraft:unknown") : it->second;
            info.count = raw.count;
            info.aux = static_cast<int>(raw.metadata);
            info.blockRuntimeId = raw.blockRuntimeId;
            if (raw.blockRuntimeId != 0 && it != itemNames.end()) {
                auto& pal = BlockPalette::Instance();
                const int32_t hash = static_cast<int32_t>(raw.blockRuntimeId);
                if (pal.Name(pal.Intern(hash)).empty() && pal.LearnName(hash, it->second)) {
                    std::cout << "[blocks] learned " << it->second << " for block id " << hash << " from an item stack" << std::endl;
                    blockNamesLearned = true;
                }
            }
            if (const auto* nbt = ItemNbt(raw)) {
                int damage = 0;
                FindNbtString(*nbt, "Name", info.customName);
                if (FindNbtInt(*nbt, "Damage", damage))
                    info.damage = std::max(0, damage);
                info.enchanted = Contains(*nbt, "ench") || Contains(*nbt, "Enchantments") || Contains(*nbt, "StoredEnchantments") ||
                                 Contains(*nbt, "minecraft:enchantments");
            }
            static const char* const kAlwaysGlint[] = {"minecraft:enchanted_golden_apple", "minecraft:experience_bottle",
                                                       "minecraft:nether_star", "minecraft:end_crystal", "minecraft:enchanted_book"};
            for (const char* name : kAlwaysGlint)
                if (info.name == name)
                    info.enchanted = true;
            return info;
        };

        const auto store = [this, convert](const WindowIDVarint window, const std::optional<FullContainerName>& container,
                                           int firstSlot, const std::vector<ItemV4>& items, bool whole) {
            std::lock_guard lock(statusMutex);
            const ContainerSlotType type = container ? container->containerId : ContainerSlotType::HotbarAndInventory;
            const bool offhand = window == WindowIDVarint::Offhand || type == ContainerSlotType::Offhand;
            const bool armor = window == WindowIDVarint::Armor || type == ContainerSlotType::Armor;
            const bool playerInventory = window == WindowIDVarint::Inventory || window == WindowIDVarint::Hotbar ||
                                         window == WindowIDVarint::FixedInventory || type == ContainerSlotType::Hotbar ||
                                         type == ContainerSlotType::Inventory || type == ContainerSlotType::HotbarAndInventory;
            static const bool debug = PERLIN_ENV("PERLIN_INV_DEBUG") != nullptr;
            if (debug)
                std::cout << "[inv] window " << static_cast<uint32_t>(window) << " container " << static_cast<int>(type) << " first "
                          << firstSlot << " n " << items.size() << (whole ? " (content)" : " (slot)") << std::endl;
            const bool mainOnly = type == ContainerSlotType::Inventory && whole && items.size() == 27;
            const bool uiWindow = window == WindowIDVarint::Ui || type == ContainerSlotType::Cursor || type == ContainerSlotType::CraftingInput ||
                                  type == ContainerSlotType::CraftingOutput || type == ContainerSlotType::CreativeOutput;
            for (size_t i = 0; i < items.size(); i++) {
                const int slot = firstSlot + static_cast<int>(i);
                const ItemStackInfo info = convert(items[i]);
                if (uiWindow) {
                    int addr = -1;
                    if (type == ContainerSlotType::Cursor || (window == WindowIDVarint::Ui && !container && slot == 0))
                        addr = kInvCursor;
                    else if (slot >= 28 && slot <= 31 && (type == ContainerSlotType::CraftingInput || window == WindowIDVarint::Ui))
                        addr = kInvCraft0 + slot - 28;
                    else if (type == ContainerSlotType::CraftingOutput || type == ContainerSlotType::CreativeOutput)
                        addr = kInvOutput;
                    if (addr >= 0) {
                        InvInfoLocked(addr) = info;
                        InvRawLocked(addr) = items[i];
                        if (addr >= kInvCraft0 && addr < kInvOutput)
                            InvRecomputeOutputLocked();
                    }
                } else if (offhand) {
                    if (slot == 0) { inventory.offhand = info; extraRaw[kInvOffhand - 36] = items[i]; }
                } else if (armor) {
                    if (slot >= 0 && slot < 4) { inventory.armor[slot] = info; extraRaw[kInvArmor0 - 36 + slot] = items[i]; }
                } else if (playerInventory && window != WindowIDVarint::Ui) {
                    if (mainOnly) {
                        if (slot >= 0 && slot < 27) { inventory.main[slot] = info; inventoryRaw[slot + 9] = items[i]; }
                    } else if (slot >= 0 && slot < 9) {
                        inventory.hotbar[slot] = info;
                        inventoryRaw[slot] = items[i];
                    } else if (slot >= 9 && slot < 36) {
                        inventory.main[slot - 9] = info;
                        inventoryRaw[slot] = items[i];
                    }
                }
            }
        };

        On<InventoryContentPacket>([store](const InventoryContentPacket& p) {
            store(p.windowId, p.container, 0, p.input, true);
        });
        On<InventorySlotPacket>([store](const InventorySlotPacket& p) {
            store(p.windowId, p.container, static_cast<int>(p.slot), std::vector<ItemV4>{p.item}, false);
        });

        On<ItemStackResponsePacket>([this](const ItemStackResponsePacket& p) {
            std::vector<std::array<int, 3>> retries;
            {
            std::lock_guard lock(statusMutex);
            static const bool debug = PERLIN_ENV("PERLIN_INV_DEBUG") != nullptr;
            for (const ItemStackResponsesEntry& r : p.responses) {
                if (const auto pending = pendingDrops.find(r.requestId); pending != pendingDrops.end()) {
                    if (r.status == ItemStackResponsesEntryStatus::Ok) {
                        dropForm = pending->second.attempt;
                    } else if (pending->second.attempt < 2) {
                        retries.push_back({pending->second.slot, pending->second.count, pending->second.attempt + 1});
                    }
                    pendingDrops.erase(pending);
                }
                if (r.status != ItemStackResponsesEntryStatus::Ok)
                    std::cout << "[inventory] request " << r.requestId << " rejected, status " << static_cast<int>(r.status) << ":"
                              << (invSummaries.count(r.requestId) ? invSummaries[r.requestId] : std::string()) << std::endl;
                else if (debug)
                    std::cout << "[inventory] request " << r.requestId << " ok" << std::endl;
                if (r.status != ItemStackResponsesEntryStatus::Ok || !r.containers)
                    continue;
                for (const auto& c : *r.containers) {
                    const ContainerSlotType type = c.slotType.containerId;
                    for (const auto& s : c.slots) {
                        int slot = -1;
                        switch (type) {
                        case ContainerSlotType::HotbarAndInventory:
                        case ContainerSlotType::Hotbar: slot = s.slot; break;
                        case ContainerSlotType::Inventory: slot = s.slot; break;
                        case ContainerSlotType::Armor: slot = kInvArmor0 + s.slot; break;
                        case ContainerSlotType::Offhand: slot = kInvOffhand; break;
                        case ContainerSlotType::CraftingInput: slot = s.slot >= 28 && s.slot <= 31 ? kInvCraft0 + s.slot - 28 : -1; break;
                        case ContainerSlotType::CraftingOutput:
                        case ContainerSlotType::CreativeOutput: slot = kInvOutput; break;
                        case ContainerSlotType::Cursor: slot = kInvCursor; break;
                        default: break;
                        }
                        if (slot < 0 || slot >= kInvSlots)
                            continue;
                        ItemStackInfo& info = InvInfoLocked(slot);
                        ItemV4& raw = InvRawLocked(slot);
                        if (s.count == 0) {
                            info = ItemStackInfo{};
                            raw = ItemV4{};
                        } else {
                            info.count = s.count;
                            raw.count = s.count;
                            if (s.itemStackId) {
                                raw.stackId = s.itemStackId;
                                raw.hasStackId = true;
                            }
                        }
                    }
                }
                InvRecomputeOutputLocked();
            }
            }
            for (const auto& r : retries)
                SendDropRequest(r[0], r[1], r[2]);
            {
                std::lock_guard lock(statusMutex);
                for (const ItemStackResponsesEntry& r : p.responses)
                    if (r.requestId == invPendingRequest)
                        invPendingRequest = 0;
            }
            InvPump();
        });

        On<CreativeContentPacket>([this](const CreativeContentPacket& p) {
            std::lock_guard lock(statusMutex);
            const auto info = [this](const ItemLegacy& o) {
                ItemStackInfo r;
                const auto it = itemNames.find(o.networkId);
                if (o.networkId != 0) {
                    r.name = it == itemNames.end() ? std::string("minecraft:unknown") : it->second;
                    r.count = std::max<int>(1, o.count);
                    r.aux = static_cast<int>(o.metadata);
                    r.blockRuntimeId = static_cast<uint32_t>(o.blockRuntimeId);
                }
                return r;
            };
            CreativeCatalog next;
            creativeRaw.clear();
            for (const auto& g : p.groups) {
                CreativeGroup group;
                group.category = static_cast<int>(g.category);
                group.name = g.name;
                group.icon = info(g.iconItem);
                next.groups.push_back(std::move(group));
            }
            for (const auto& i : p.items) {
                CreativeItem item;
                item.entryId = i.entryId;
                item.info = info(i.item);
                item.group = static_cast<int>(i.groupIndex);
                if (item.info.name.empty())
                    continue;
                ItemV4 raw;
                raw.networkId = static_cast<int16_t>(i.item.networkId);
                raw.count = static_cast<uint16_t>(item.info.count);
                raw.metadata = i.item.metadata;
                raw.blockRuntimeId = static_cast<uint32_t>(i.item.blockRuntimeId);
                creativeRaw[i.entryId] = raw;
                next.items.push_back(std::move(item));
            }
            next.version = creative.version + 1;
            creative = std::move(next);
        });

        On<CraftingDataPacket>([this](const CraftingDataPacket& p) {
            const auto ingredient = [](const RecipeIngredient& in) {
                RecipeIngredientInfo r;
                if (in.type == RecipeIngredientType::Valid && (in.descriptorType == "name" || in.descriptorType == "item_tag")) {
                    r.empty = false;
                    r.name = in.name;
                    r.tag = in.tag;
                    r.meta = in.metadata;
                }
                return r;
            };
            std::vector<RecipeInfo> list;
            const auto outInfo = [this](const ItemLegacy& o) {
                ItemStackInfo info;
                const auto it = itemNames.find(o.networkId);
                if (o.networkId != 0 && o.count > 0) {
                    info.name = it == itemNames.end() ? std::string("minecraft:unknown") : it->second;
                    info.count = o.count;
                    info.aux = static_cast<int>(o.metadata);
                    info.blockRuntimeId = static_cast<uint32_t>(o.blockRuntimeId);
                }
                return info;
            };
            const auto outRaw = [](const ItemLegacy& o) {
                ItemV4 raw;
                raw.networkId = static_cast<int16_t>(o.networkId);
                raw.count = o.count;
                raw.metadata = o.metadata;
                raw.blockRuntimeId = static_cast<uint32_t>(o.blockRuntimeId);
                return raw;
            };
            for (const ShapedRecipe& r : p.shapedRecipes) {
                if (r.block != "crafting_table" || r.width > 2 || r.height > 2 || r.output.empty())
                    continue;
                RecipeInfo ri;
                ri.shaped = true;
                ri.w = r.width;
                ri.h = r.height;
                for (const auto& in : r.input)
                    ri.in.push_back(ingredient(in));
                ri.out = outRaw(r.output[0]);
                ri.outInfo = outInfo(r.output[0]);
                ri.netId = r.networkId;
                list.push_back(std::move(ri));
            }
            for (const ShapelessRecipe& r : p.shapelessRecipes) {
                if (r.block != "crafting_table" || r.input.size() > 4 || r.output.empty())
                    continue;
                RecipeInfo ri;
                for (const auto& in : r.input)
                    ri.in.push_back(ingredient(in));
                ri.out = outRaw(r.output[0]);
                ri.outInfo = outInfo(r.output[0]);
                ri.netId = r.networkId;
                list.push_back(std::move(ri));
            }
            std::lock_guard lock(statusMutex);
            craftedRecipe = -1;
            if (p.clearRecipes)
                recipes.clear();
            for (auto& r : list)
                recipes.push_back(std::move(r));
            std::cout << "[inventory] " << recipes.size() << " recipes fit the 2x2 grid" << std::endl;
        });

        On<ContainerOpenPacket>([this](const ContainerOpenPacket& p) {
            std::cout << "[bedrock] container open window " << static_cast<int>(p.windowId) << " type " << static_cast<int>(p.windowType) << std::endl;
            std::lock_guard lock(statusMutex);
            inventoryWindow = static_cast<int>(p.windowId);
            inventoryOpen = true;
        });
        On<ContainerClosePacket>([this](const ContainerClosePacket& p) {
            std::cout << "[bedrock] container close window " << static_cast<int>(p.windowId) << (p.server ? " (server)" : "") << std::endl;
            std::lock_guard lock(statusMutex);
            inventoryOpen = false;
            if (p.server)
                serverClosedInventory = true;
        });

        On<PlayerHotbarPacket>([this](const PlayerHotbarPacket& p) {
            if (!p.selectSlot)
                return;
            std::lock_guard lock(statusMutex);
            inventory.selected = static_cast<int>(std::min<uint32_t>(p.selectedSlot, 8));
        });

        On<MobEquipmentPacket>([this, convert](const MobEquipmentPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId)
                return;
            std::lock_guard lock(statusMutex);
            const int selected = std::min<int>(p.selectedSlot, 8);
            inventory.selected = selected;
            if (p.slot < 9) {
                inventory.hotbar[p.slot] = convert(p.item);
                inventoryRaw[p.slot] = p.item;
            }
        });

        On<MobArmorEquipmentPacket>([this, convert](const MobArmorEquipmentPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId)
                return;
            std::lock_guard lock(statusMutex);
            inventory.armor[0] = convert(p.helmet);
            inventory.armor[1] = convert(p.chestplate);
            inventory.armor[2] = convert(p.leggings);
            inventory.armor[3] = convert(p.boots);
        });

        On<UpdateAttributesPacket>([this](const UpdateAttributesPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId)
                return;
            std::lock_guard lock(statusMutex);
            for (const PlayerAttributesEntry& a : p.attributes) {
                const std::string name = a.name.rfind("minecraft:", 0) == 0 ? a.name.substr(10) : a.name;
                if (name == "health") {
                    vitals.health = a.current;
                    vitals.maxHealth = a.max > 0 ? a.max : 20.0f;
                    vitals.known = true;
                } else if (name == "player.hunger") {
                    vitals.hunger = a.current;
                } else if (name == "player.saturation") {
                    vitals.saturation = a.current;
                } else if (name == "absorption") {
                    vitals.absorption = a.current;
                } else if (name == "player.level") {
                    vitals.level = static_cast<int>(a.current);
                } else if (name == "player.experience") {
                    vitals.xpProgress = std::clamp(a.current, 0.0f, 1.0f);
                }
            }
        });

        On<MobEffectPacket>([this](const MobEffectPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId)
                return;
            const bool on = p.eventId != MobEffectPacketEventId::Remove;
            std::lock_guard lock(statusMutex);
            switch (p.effectId) {
            case 10: vitals.regeneration = on; break;
            case 17: vitals.hungerEffect = on; break;
            case 19: vitals.poison = on; break;
            case 20: vitals.wither = on; break;
            default: break;
            }
        });

        On<SetEntityDataPacket>([this](const SetEntityDataPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId != runtimeId)
                return;
            std::lock_guard lock(statusMutex);
            for (const MetadataDictionaryEntry& e : p.metadata) {
                int value = 0;
                if (e.key == MetadataDictionaryEntryKey::Flags && IntegerOf(e, value))
                    vitals.onFire = (value & 1) != 0;
                else if (e.key == MetadataDictionaryEntryKey::Air && IntegerOf(e, value))
                    vitals.air = value;
                else if (e.key == MetadataDictionaryEntryKey::MaxAirdataMaxAir && IntegerOf(e, value) && value > 0)
                    vitals.maxAir = value;
            }
        });

        On<SetTitlePacket>([this](const SetTitlePacket& p) {
            std::lock_guard lock(statusMutex);
            const auto now = std::chrono::steady_clock::now();
            switch (p.type) {
            case SetTitlePacketType::Clear:
            case SetTitlePacketType::Reset:
                titleText.title.clear();
                titleText.subtitle.clear();
                titleText.actionBar.clear();
                titleText.serial++;
                break;
            case SetTitlePacketType::SetTitle:
            case SetTitlePacketType::SetTitleJson:
                titleText.title = p.text;
                titleText.titleAt = now;
                titleText.serial++;
                break;
            case SetTitlePacketType::SetSubtitle:
            case SetTitlePacketType::SetSubtitleJson:
                titleText.subtitle = p.text;
                titleText.titleAt = now;
                titleText.serial++;
                break;
            case SetTitlePacketType::ActionBarMessage:
            case SetTitlePacketType::ActionBarMessageJson:
                titleText.actionBar = p.text;
                titleText.actionAt = now;
                titleText.serial++;
                break;
            case SetTitlePacketType::SetDurations:
                titleText.fadeIn = p.fadeInTime;
                titleText.stay = p.stayTime;
                titleText.fadeOut = p.fadeOutTime;
                break;
            default:
                break;
            }
        });

        OnPartial<TextPacket>([this](const TextPacket& t) {
            if (t.type == TextPacketType::Popup || t.type == TextPacketType::Tip || t.type == TextPacketType::JukeboxPopup) {
                std::lock_guard lock(statusMutex);
                titleText.actionBar = t.message;
                titleText.actionAt = std::chrono::steady_clock::now();
                titleText.serial++;
            }
        });
    }
}

namespace perlin {
    void BedrockSession::ClearCrack(int x, int y, int z) {
        std::lock_guard lock(crackMutex);
        std::erase_if(crackStates, [&](const CrackState& c) { return c.x == x && c.y == y && c.z == z; });
    }

    std::vector<BedrockSession::BlockCrack> BedrockSession::Cracks() {
        std::lock_guard lock(crackMutex);
        const auto now = std::chrono::steady_clock::now();
        std::vector<BlockCrack> out;
        for (auto it = crackStates.begin(); it != crackStates.end();) {
            const double age = std::chrono::duration<double>(now - it->since).count();
            if (age > 15.0) {
                it = crackStates.erase(it);
                continue;
            }
            const double raw = it->progress + it->perSecond * age;
            if (raw > 1.25) {
                it = crackStates.erase(it);
                continue;
            }
            const double progress = std::clamp(raw, 0.0, 1.0);
            out.push_back({it->x, it->y, it->z, std::min(9, static_cast<int>(progress * 10.0))});
            ++it;
        }
        return out;
    }
}

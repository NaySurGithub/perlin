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
                if (FindNbtInt(*nbt, "Damage", damage))
                    info.damage = std::max(0, damage);
                info.enchanted = Contains(*nbt, "ench") || Contains(*nbt, "Enchantments");
            }
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
            static const bool debug = std::getenv("PERLIN_INV_DEBUG") != nullptr;
            if (debug)
                std::cout << "[inv] window " << static_cast<uint32_t>(window) << " container " << static_cast<int>(type) << " first "
                          << firstSlot << " n " << items.size() << (whole ? " (content)" : " (slot)") << std::endl;

            const bool mainOnly = type == ContainerSlotType::Inventory && whole && items.size() == 27;
            for (size_t i = 0; i < items.size(); i++) {
                const int slot = firstSlot + static_cast<int>(i);
                const ItemStackInfo info = convert(items[i]);
                if (offhand) {
                    if (slot == 0) inventory.offhand = info;
                } else if (armor) {
                    if (slot >= 0 && slot < 4) inventory.armor[slot] = info;
                } else if (playerInventory && window != WindowIDVarint::Ui) {
                    if (mainOnly) {
                        if (slot >= 0 && slot < 27) inventory.main[slot] = info;
                    } else if (slot >= 0 && slot < 9) {
                        inventory.hotbar[slot] = info;
                        inventoryRaw[slot] = items[i];
                    } else if (slot >= 9 && slot < 36) {
                        inventory.main[slot - 9] = info;
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
            std::lock_guard lock(statusMutex);
            static const bool debug = std::getenv("PERLIN_INV_DEBUG") != nullptr;
            for (const ItemStackResponsesEntry& r : p.responses) {

                if (r.status != ItemStackResponsesEntryStatus::Ok)
                    std::cout << "[inventory] request " << r.requestId << " rejected, status " << static_cast<int>(r.status) << std::endl;
                else if (debug)
                    std::cout << "[inventory] request " << r.requestId << " ok" << std::endl;
                if (r.status != ItemStackResponsesEntryStatus::Ok || !r.containers)
                    continue;
                for (const auto& c : *r.containers) {
                    const ContainerSlotType type = c.slotType.containerId;
                    if (type != ContainerSlotType::HotbarAndInventory && type != ContainerSlotType::Hotbar &&
                        type != ContainerSlotType::Inventory)
                        continue;
                    for (const auto& s : c.slots) {
                        const int slot = type == ContainerSlotType::Inventory ? s.slot + 9 : s.slot;
                        ItemStackInfo* info = slot < 9 ? &inventory.hotbar[slot] : slot < 36 ? &inventory.main[slot - 9] : nullptr;
                        if (!info)
                            continue;
                        if (s.count == 0) {
                            *info = ItemStackInfo{};
                            if (slot < 9)
                                inventoryRaw[slot] = ItemV4{};
                        } else {
                            info->count = s.count;
                            if (slot < 9) {
                                inventoryRaw[slot].count = s.count;
                                if (s.itemStackId)
                                    inventoryRaw[slot].stackId = s.itemStackId;
                            }
                        }
                    }
                }
            }
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
                if (e.key == MetadataDictionaryEntryKey::Air && IntegerOf(e, value))
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
            const double progress = std::clamp(it->progress + it->perSecond * age, 0.0, 1.0);
            out.push_back({it->x, it->y, it->z, std::min(9, static_cast<int>(progress * 10.0))});
            ++it;
        }
        return out;
    }
}

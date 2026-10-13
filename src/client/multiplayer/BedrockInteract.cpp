#include "client/multiplayer/BedrockSession.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <vector>
#include "world/level/block/BlockPalette.hpp"

namespace perlin {
    using namespace proto;

    namespace {
        Vec3F V(const float p[3]) { return {p[0], p[1], p[2]}; }
    }

    bool BedrockSession::SendUseItemOnBlock(int bx, int by, int bz, int face, const float click[3], const float eye[3], int use, const int dest[3]) {
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemUse;
        TransactionUseItem u;
        int selected = 0;
        {
            std::lock_guard lock(statusMutex);
            selected = std::clamp(inventory.selected, 0, 8);
            u.heldItem = inventoryRaw[selected];
        }
        u.actionType = TransactionUseItemActionType::ClickBlock;
        u.triggerType = TransactionUseItemTriggerType::PlayerInput;
        u.blockPosition = {bx, by, bz};
        u.face = static_cast<uint8_t>(face);
        u.hotbarSlot = selected;
        u.hand = TransactionUseItemHand::MainHand;
        u.playerPos = V(eye);
        u.clickPos = V(click);

        {
            bool loaded = false;
            const uint16_t clicked = netLevel.GetBlock(bx, by, bz, loaded);
            u.blockRuntimeId = loaded ? static_cast<uint32_t>(BlockPalette::Instance().NetworkId(clicked)) : 0;
        }
        haveLoggedInteraction = true;
        std::cout << "[place] use item on (" << bx << "," << by << "," << bz << ") face " << face << " clicked id " << u.blockRuntimeId
                  << " held " << u.heldItem.networkId << " x" << static_cast<int>(u.heldItem.count) << " block " << u.heldItem.blockRuntimeId
                  << " use " << (use == 2 ? "place" : use == 1 ? "interact" : "nothing") << " dest (" << dest[0] << "," << dest[1] << "," << dest[2]
                  << ")" << std::endl;
        if (use == 2 && u.heldItem.blockRuntimeId != 0) {
            std::lock_guard lock(placeMutex);
            pendingPlace = {dest[0], dest[1], dest[2], std::chrono::steady_clock::now(), true, static_cast<int32_t>(u.heldItem.blockRuntimeId)};
        }
        if (use != 0 && !itemUseOnActive.exchange(true)) {

            PlayerActionPacket start;
            start.runtimeEntityId = runtimeId;
            start.action = Action::StartItemUseOn;
            start.position = {bx, by, bz};
            start.resultPosition = {dest[0], dest[1], dest[2]};
            start.face = face;
            Send(start);
            itemUseDest[0] = dest[0];
            itemUseDest[1] = dest[1];
            itemUseDest[2] = dest[2];
        }
        u.clientPrediction = use != 0 ? TransactionUseItemClientPrediction::Success : TransactionUseItemClientPrediction::Failure;
        p.transaction.transactionData = u;
        return Send(p);
    }

    bool BedrockSession::SendUseItemInAir(const float eye[3]) {
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemUse;
        TransactionUseItem u;
        int selected = 0;
        {
            std::lock_guard lock(statusMutex);
            selected = std::clamp(inventory.selected, 0, 8);
            u.heldItem = inventoryRaw[selected];
        }
        u.actionType = TransactionUseItemActionType::ClickAir;
        u.triggerType = TransactionUseItemTriggerType::PlayerInput;
        u.blockPosition = {0, 0, 0};
        u.face = 255;
        u.hotbarSlot = selected;
        u.hand = TransactionUseItemHand::MainHand;
        u.playerPos = V(eye);
        u.clickPos = {0, 0, 0};
        u.blockRuntimeId = 0;
        p.transaction.transactionData = u;
        return Send(p);
    }

    bool BedrockSession::SendReleaseItem(const float eye[3]) {
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemRelease;
        TransactionTransactionDataCase2 r;
        {
            std::lock_guard lock(statusMutex);
            r.hotbarSlot = std::clamp(inventory.selected, 0, 8);
            r.heldItem = inventoryRaw[r.hotbarSlot];
        }
        r.actionType = TransactionTransactionDataCase2ActionType::Release;
        r.headPos = V(eye);
        p.transaction.transactionData = r;
        return Send(p);
    }

    bool BedrockSession::SendAttackEntity(uint64_t targetRuntimeId, const float eye[3], const float click[3]) {
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemUseOnEntity;
        TransactionTransactionDataCase1 a;
        {
            std::lock_guard lock(statusMutex);
            a.hotbarSlot = std::clamp(inventory.selected, 0, 8);
            a.heldItem = inventoryRaw[a.hotbarSlot];
        }
        a.entityRuntimeId = targetRuntimeId;
        a.actionType = TransactionTransactionDataCase1ActionType::Attack;
        a.playerPos = V(eye);
        a.clickPos = V(click);
        p.transaction.transactionData = a;
        return Send(p);
    }

    bool BedrockSession::SendPickBlock(int bx, int by, int bz) {
        BlockPickRequestPacket p;
        p.x = bx;
        p.y = by;
        p.z = bz;
        p.addUserData = false;
        {
            std::lock_guard lock(statusMutex);
            p.selectedSlot = static_cast<uint8_t>(std::clamp(inventory.selected, 0, 8));
        }
        return Send(p);
    }

    bool BedrockSession::SendDropSelected(int count) {
        ItemStackRequestPacket p;
        ItemStackRequest r;
        ItemStackRequestActionsEntry a;
        {
            std::lock_guard lock(statusMutex);
            const int slot = std::clamp(inventory.selected, 0, 8);
            const ItemV4& raw = inventoryRaw[slot];
            if (raw.networkId == 0 || raw.count == 0)
                return false;
            a.source.slotType.containerId = ContainerSlotType::HotbarAndInventory;
            a.source.slot = static_cast<uint8_t>(slot);
            a.source.stackId = raw.stackId.value_or(0);
            a.count = static_cast<uint8_t>(count <= 0 ? std::min<int>(raw.count, 255) : std::min<int>(count, raw.count));
        }
        a.typeId = ItemStackRequestActionsEntryTypeId::Drop;
        a.legacyTypeId = static_cast<uint8_t>(ItemStackRequestActionsEntryTypeId::Drop);
        a.randomly = false;
        r.requestId = nextStackRequestId;
        nextStackRequestId -= 2;
        r.actions.push_back(a);
        r.cause = static_cast<ItemStackRequestCause>(-1);
        std::cout << "[inventory] drop request " << r.requestId << " slot " << static_cast<int>(a.source.slot) << " stack " << a.source.stackId
                  << " count " << static_cast<int>(a.count) << std::endl;
        p.requests.push_back(std::move(r));
        return Send(p);
    }
}

namespace perlin {

    void BedrockSession::ConfirmPlace(int x, int y, int z, int32_t runtimeId) {
        int32_t held = 0;
        {
            std::lock_guard lock(placeMutex);
            if (!pendingPlace.active)
                return;
            if (std::chrono::steady_clock::now() - pendingPlace.at > std::chrono::seconds(2)) {
                pendingPlace.active = false;
                return;
            }
            if (pendingPlace.x != x || pendingPlace.y != y || pendingPlace.z != z)
                return;
            pendingPlace.active = false;
            held = pendingPlace.held;
        }
        {
            auto& pal = BlockPalette::Instance();
            std::cout << "[place] server put block " << runtimeId << " (" << pal.Name(pal.Intern(runtimeId)) << ") at (" << x << "," << y << ","
                      << z << "), we placed with block id " << held << " (" << pal.Name(pal.Intern(held)) << ")"
                      << (runtimeId != held ? "  <-- different" : "") << std::endl;
        }
        if (runtimeId == BlockPalette::AirHash())
            return;
        SoundRequest r;
        r.kind = SoundRequest::Kind::Level;
        r.name = "place";
        r.x = x + 0.5f;
        r.y = y + 0.5f;
        r.z = z + 0.5f;
        r.extra = runtimeId;
        PushSound(std::move(r));
    }
}

namespace perlin {
    void BedrockSession::NoteUnknownBlock(int32_t networkId) {
        static std::mutex m;
        static std::vector<int32_t> seen;
        auto& pal = BlockPalette::Instance();
        if (!pal.Name(pal.Intern(networkId)).empty())
            return;
        std::lock_guard lock(m);
        if (std::find(seen.begin(), seen.end(), networkId) != seen.end() || seen.size() > 200)
            return;
        seen.push_back(networkId);
        std::cout << "[block] server sent block id " << networkId << " that matches no known block state" << std::endl;
    }
}

namespace perlin {
    using namespace proto;
    void BedrockSession::EndItemUseOn() {
        if (!itemUseOnActive.exchange(false))
            return;
        PlayerActionPacket stop;
        stop.runtimeEntityId = runtimeId;
        stop.action = Action::StopItemUseOn;
        stop.position = {itemUseDest[0], itemUseDest[1], itemUseDest[2]};
        stop.resultPosition = {0, 0u, 0};
        stop.face = 0;
        Send(stop);
    }
}

#include "client/multiplayer/BedrockSession.hpp"

#include <algorithm>
#include <iterator>
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
        if (use == 2) {
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

    bool BedrockSession::SendInteractEntity(uint64_t targetRuntimeId, const float eye[3], const float click[3]) {
        InventoryTransactionPacket p;
        p.transaction.transactionType = TransactionTransactionType::ItemUseOnEntity;
        TransactionTransactionDataCase1 a;
        {
            std::lock_guard lock(statusMutex);
            a.hotbarSlot = std::clamp(inventory.selected, 0, 8);
            a.heldItem = inventoryRaw[a.hotbarSlot];
        }
        a.entityRuntimeId = targetRuntimeId;
        a.actionType = TransactionTransactionDataCase1ActionType::Interact;
        a.playerPos = V(eye);
        a.clickPos = V(click);
        p.transaction.transactionData = a;
        std::cout << "[interact] entity " << targetRuntimeId << std::endl;
        return Send(p);
    }

    bool BedrockSession::SendLeaveVehicle() {
        const int64_t uid = ridingUid.load();
        if (uid == 0)
            return false;
        entity::Entity vehicle;
        if (!entities.FindUnique(uid, vehicle))
            return false;
        InteractPacket p;
        p.actionId = InteractPacketActionId::LeaveVehicle;
        p.targetEntityId = vehicle.runtimeId;
        p.hasPosition = false;
        std::cout << "[interact] leave vehicle " << vehicle.runtimeId << std::endl;
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

    bool BedrockSession::SendDropRequest(int slot, int count, int attempt) {
        ItemStackRequestPacket p;
        ItemStackRequest r;
        ItemStackRequestActionsEntry a;
        {
            std::lock_guard lock(statusMutex);
            const ItemV4& raw = inventoryRaw[slot];
            if (raw.networkId == 0 || raw.count == 0)
                return false;
            if (attempt < 0)
                attempt = dropForm;
            if (attempt == 1) {
                a.source.slotType.containerId = slot < 9 ? ContainerSlotType::Hotbar : ContainerSlotType::Inventory;
                a.source.slot = static_cast<uint8_t>(slot < 9 ? slot : slot - 9);
            } else {
                a.source.slotType.containerId = ContainerSlotType::HotbarAndInventory;
                a.source.slot = static_cast<uint8_t>(slot);
            }
            a.source.stackId = attempt >= 2 ? 0 : raw.stackId.value_or(0);
            a.count = static_cast<uint8_t>(count <= 0 ? std::min<int>(raw.count, 255) : std::min<int>(count, raw.count));
            r.requestId = nextStackRequestId;
            nextStackRequestId -= 2;
            pendingDrops[r.requestId] = {slot, a.count, attempt};
            if (pendingDrops.size() > 64)
                pendingDrops.erase(std::prev(pendingDrops.end()));
        }
        a.typeId = ItemStackRequestActionsEntryTypeId::Drop;
        a.legacyTypeId = static_cast<uint8_t>(ItemStackRequestActionsEntryTypeId::Drop);
        a.randomly = false;
        r.actions.push_back(a);
        r.cause = static_cast<ItemStackRequestCause>(-1);
        std::cout << "[inventory] drop request " << r.requestId << " slot " << slot << " stack " << a.source.stackId << " count "
                  << static_cast<int>(a.count) << " form " << attempt << std::endl;
        p.requests.push_back(std::move(r));
        return Send(p);
    }

    bool BedrockSession::SendDropSelected(int count) {
        int slot;
        {
            std::lock_guard lock(statusMutex);
            slot = std::clamp(inventory.selected, 0, 8);
        }
        return SendDropRequest(slot, count, -1);
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

    bool BedrockSession::OpenInventory() {
        InteractPacket p;
        p.actionId = InteractPacketActionId::OpenInventory;
        p.targetEntityId = runtimeId;
        p.hasPosition = false;
        std::cout << "[inventory] open (Interact OpenInventory, runtime id " << runtimeId << ")" << std::endl;
        return Send(p);
    }

    bool BedrockSession::CloseInventory() {
        InvReturnCursor();
        {
            std::lock_guard lock(statusMutex);
            invCloseWanted = true;
        }
        InvPump();
        return true;
    }

    bool BedrockSession::SendCloseNow() {
        int window;
        {
            std::lock_guard lock(statusMutex);
            window = inventoryOpen ? inventoryWindow : 0;
            inventoryOpen = false;
        }
        ContainerClosePacket p;
        p.windowId = static_cast<WindowID>(window);
        p.windowType = WindowType::Inventory;
        p.server = false;
        std::cout << "[inventory] close window " << window << std::endl;
        return Send(p);
    }

    bool BedrockSession::TakeServerClosedInventory() {
        std::lock_guard lock(statusMutex);
        const bool v = serverClosedInventory;
        serverClosedInventory = false;
        return v;
    }

    static void FillSlot(StackRequestSlotInfo& s, int slot, const ItemV4& raw) {
        s.slotType.containerId = ContainerSlotType::HotbarAndInventory;
        s.slot = static_cast<uint8_t>(slot);
        s.stackId = raw.stackId.value_or(0);
    }

    bool BedrockSession::SwapInventorySlots(int a, int b) {
        if (a == b || a < 0 || b < 0 || a > 35 || b > 35)
            return false;
        ItemStackRequestPacket p;
        ItemStackRequest r;
        ItemStackRequestActionsEntry act;
        {
            std::lock_guard lock(statusMutex);
            FillSlot(act.source, a, inventoryRaw[a]);
            FillSlot(act.destination, b, inventoryRaw[b]);
            if (inventoryRaw[a].networkId == 0 && inventoryRaw[b].networkId == 0)
                return false;
            std::swap(inventoryRaw[a], inventoryRaw[b]);
            auto at = [&](int i) -> ItemStackInfo& { return i < 9 ? inventory.hotbar[i] : inventory.main[i - 9]; };
            std::swap(at(a), at(b));
        }
        act.typeId = ItemStackRequestActionsEntryTypeId::Swap;
        act.legacyTypeId = static_cast<uint8_t>(ItemStackRequestActionsEntryTypeId::Swap);
        r.requestId = nextStackRequestId;
        nextStackRequestId -= 2;
        r.actions.push_back(act);
        r.cause = static_cast<ItemStackRequestCause>(-1);
        std::cout << "[inventory] swap request " << r.requestId << " " << a << " <-> " << b << std::endl;
        p.requests.push_back(std::move(r));
        return Send(p);
    }

    bool BedrockSession::DropInventorySlot(int slot, bool whole) {
        if (slot < 0 || slot > 35)
            return false;
        return SendDropRequest(slot, whole ? 0 : 1, -1);
    }
}

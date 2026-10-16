#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace perlin::abi {
    inline constexpr uint32_t kApiVersion = 1;
    inline constexpr int32_t kNeedMoreBase = 16;

    enum class Op : uint32_t {
        Log = 0x0001,
        Meta = 0x0002,
        Clock = 0x0003,

        PlayerState = 0x0100,
        PlayerEffects = 0x0101,
        PlayerLook = 0x0102,

        Inventory = 0x0200,
        InventorySelect = 0x0201,
        InventoryDrop = 0x0202,
        InventorySwap = 0x0203,
        InventoryClick = 0x0204,
        InventoryOpen = 0x0205,
        InventoryClose = 0x0206,

        WorldState = 0x0300,
        Block = 0x0301,
        Region = 0x0302,
        Raycast = 0x0303,
        Aim = 0x0304,
        FindBlocks = 0x0305,
        ChunkLoaded = 0x0306,
        BlockNames = 0x0307,

        Entities = 0x0400,
        EntityById = 0x0401,
        PlayerList = 0x0402,

        Options = 0x0500,
        OptionGet = 0x0501,
        OptionSet = 0x0502,
        Screen = 0x0503,
        Translate = 0x0504,
        ReloadResources = 0x0505,

        InputDown = 0x0600,
        InputOverride = 0x0601,
        KeyRegister = 0x0602,
        InputActions = 0x0603,
        MousePosition = 0x0604,

        Chat = 0x0700,
        Command = 0x0701,
        LocalMessage = 0x0702,
        CommandRegister = 0x0703,
        CommandUnregister = 0x0704,
        ServerCommands = 0x0705,
        Toast = 0x0706,
        Title = 0x0707,

        SwingArm = 0x0800,
        UseItem = 0x0801,
        UseOnBlock = 0x0802,
        AttackEntity = 0x0803,
        InteractEntity = 0x0804,
        BreakBlock = 0x0805,
        Look = 0x0806,
        Respawn = 0x0807,
        Disconnect = 0x0808,
        PlaySound = 0x0809,
        Jump = 0x080B,

        UiSubmit = 0x0900,
        UiClear = 0x0901,
        UiMeasure = 0x0902,

        StorageRead = 0x0A00,
        StorageWrite = 0x0A01,
        StorageList = 0x0A02,
        StorageRemove = 0x0A03,
        StorageExists = 0x0A04,
        StorageMakeDir = 0x0A05,

        HttpRequest = 0x0B00,
        PacketSend = 0x0B01,
        PacketName = 0x0B02,
    };

    enum class Event : uint32_t {
        Load = 1,
        Unload = 2,
        Tick = 3,
        Frame = 4,
        ChatReceived = 5,
        ChatSending = 6,
        Connected = 7,
        Disconnected = 8,
        Key = 9,
        Command = 10,
        Damage = 11,
        Death = 12,
        Respawned = 13,
        DimensionChanged = 14,
        ScreenChanged = 15,
        SlotChanged = 16,
        EntityAdded = 17,
        EntityRemoved = 18,
        Sound = 19,
        HttpResponse = 20,
        Packet = 21,
        Title = 22,
    };

    enum class Status : int32_t {
        Ok = 0,
        UnknownOp = -1,
        Denied = -2,
        BadArguments = -3,
        Unavailable = -4,
        NotFound = -5,
        Failed = -6,
    };

    enum class Permission : uint32_t {
        None = 0,
        Network = 1u << 0,
        Packets = 1u << 1,
        Control = 1u << 2,
        Storage = 1u << 3,
    };

    enum class Screen : int32_t {
        None = 0,
        Pause = 1,
        Chat = 2,
        Inventory = 3,
        Options = 4,
        Mods = 5,
        Wardrobe = 6,
        Death = 7,
        Workbench = 8,
        Menu = 9,
        Other = 10,
    };

    enum class Draw : uint8_t {
        Rect = 1,
        Text = 2,
        Item = 3,
        Image = 4,
        Line = 5,
        Gradient = 6,
        Border = 7,
    };

    class Writer {
    public:
        Writer() = default;
        explicit Writer(size_t reserve) { data.reserve(reserve); }

        template <class T>
        Writer& put(T value) {
            static_assert(std::is_trivially_copyable_v<T>);
            const size_t at = data.size();
            data.resize(at + sizeof(T));
            std::memcpy(data.data() + at, &value, sizeof(T));
            return *this;
        }
        Writer& str(std::string_view text) {
            put<uint32_t>(static_cast<uint32_t>(text.size()));
            data.insert(data.end(), text.begin(), text.end());
            return *this;
        }
        Writer& bytes(const void* p, size_t n) {
            put<uint32_t>(static_cast<uint32_t>(n));
            const auto* b = static_cast<const uint8_t*>(p);
            data.insert(data.end(), b, b + n);
            return *this;
        }
        Writer& raw(const void* p, size_t n) {
            const auto* b = static_cast<const uint8_t*>(p);
            data.insert(data.end(), b, b + n);
            return *this;
        }
        Writer& boolean(bool v) { return put<uint8_t>(v ? 1 : 0); }
        const uint8_t* Data() const { return data.data(); }
        size_t Size() const { return data.size(); }
        void Clear() { data.clear(); }
        std::vector<uint8_t> data;
    };

    class Reader {
    public:
        Reader() = default;
        Reader(const uint8_t* p, size_t n) : cur(p), end(p + n) {}

        template <class T>
        T get() {
            T value{};
            if (static_cast<size_t>(end - cur) < sizeof(T)) {
                failed = true;
                cur = end;
                return value;
            }
            std::memcpy(&value, cur, sizeof(T));
            cur += sizeof(T);
            return value;
        }
        std::string str() {
            const uint32_t n = get<uint32_t>();
            if (failed || static_cast<size_t>(end - cur) < n) {
                failed = true;
                cur = end;
                return {};
            }
            std::string out(reinterpret_cast<const char*>(cur), n);
            cur += n;
            return out;
        }
        std::vector<uint8_t> bytes() {
            const uint32_t n = get<uint32_t>();
            if (failed || static_cast<size_t>(end - cur) < n) {
                failed = true;
                cur = end;
                return {};
            }
            std::vector<uint8_t> out(cur, cur + n);
            cur += n;
            return out;
        }
        bool boolean() { return get<uint8_t>() != 0; }
        size_t Remaining() const { return static_cast<size_t>(end - cur); }
        bool Ok() const { return !failed; }
        const uint8_t* Cursor() const { return cur; }
        void Skip(size_t n) {
            if (Remaining() < n) {
                failed = true;
                cur = end;
            } else {
                cur += n;
            }
        }

    private:
        const uint8_t* cur = nullptr;
        const uint8_t* end = nullptr;
        bool failed = false;
    };

    struct Item {
        std::string name;
        int32_t count = 0, aux = 0, damage = 0;
        bool enchanted = false;
        std::string customName;
        std::vector<std::pair<int32_t, int32_t>> enchants;
        std::vector<std::string> lore;

        bool Empty() const { return name.empty() || count <= 0; }
        void Write(Writer& w) const {
            w.str(name).put(count).put(aux).put(damage).boolean(enchanted).str(customName);
            w.put<uint32_t>(static_cast<uint32_t>(enchants.size()));
            for (const auto& e : enchants)
                w.put(e.first).put(e.second);
            w.put<uint32_t>(static_cast<uint32_t>(lore.size()));
            for (const auto& l : lore)
                w.str(l);
        }
        static Item Read(Reader& r) {
            Item i;
            i.name = r.str();
            i.count = r.get<int32_t>();
            i.aux = r.get<int32_t>();
            i.damage = r.get<int32_t>();
            i.enchanted = r.boolean();
            i.customName = r.str();
            const uint32_t ne = r.get<uint32_t>();
            for (uint32_t k = 0; k < ne && r.Ok(); k++) {
                const int32_t id = r.get<int32_t>();
                const int32_t lv = r.get<int32_t>();
                i.enchants.emplace_back(id, lv);
            }
            const uint32_t nl = r.get<uint32_t>();
            for (uint32_t k = 0; k < nl && r.Ok(); k++)
                i.lore.push_back(r.str());
            return i;
        }
    };

    struct PlayerState {
        uint64_t runtimeId = 0;
        int64_t uniqueId = 0;
        std::string name;
        double x = 0, y = 0, z = 0;
        double vx = 0, vy = 0, vz = 0;
        float yaw = 0, pitch = 0, headYaw = 0, eyeHeight = 1.62f;
        bool onGround = false, sprinting = false, sneaking = false, swimming = false, gliding = false, flying = false, mayFly = false,
             spectator = false, onFire = false, riding = false, connected = false;
        int32_t gameMode = 0, dimension = 0, selectedSlot = 0, permission = -1;
        float health = 20, maxHealth = 20, absorption = 0, hunger = 20, saturation = 5, xpProgress = 0, walkSpeed = 0.1f;
        int32_t xpLevel = 0, air = 300, maxAir = 300;

        void Write(Writer& w) const {
            w.put(runtimeId).put(uniqueId).str(name);
            w.put(x).put(y).put(z).put(vx).put(vy).put(vz);
            w.put(yaw).put(pitch).put(headYaw).put(eyeHeight);
            for (bool b : {onGround, sprinting, sneaking, swimming, gliding, flying, mayFly, spectator, onFire, riding, connected})
                w.boolean(b);
            w.put(gameMode).put(dimension).put(selectedSlot).put(permission);
            w.put(health).put(maxHealth).put(absorption).put(hunger).put(saturation).put(xpProgress).put(walkSpeed);
            w.put(xpLevel).put(air).put(maxAir);
        }
        static PlayerState Read(Reader& r) {
            PlayerState p;
            p.runtimeId = r.get<uint64_t>();
            p.uniqueId = r.get<int64_t>();
            p.name = r.str();
            p.x = r.get<double>(); p.y = r.get<double>(); p.z = r.get<double>();
            p.vx = r.get<double>(); p.vy = r.get<double>(); p.vz = r.get<double>();
            p.yaw = r.get<float>(); p.pitch = r.get<float>(); p.headYaw = r.get<float>(); p.eyeHeight = r.get<float>();
            for (bool* b : {&p.onGround, &p.sprinting, &p.sneaking, &p.swimming, &p.gliding, &p.flying, &p.mayFly, &p.spectator, &p.onFire, &p.riding, &p.connected})
                *b = r.boolean();
            p.gameMode = r.get<int32_t>(); p.dimension = r.get<int32_t>(); p.selectedSlot = r.get<int32_t>(); p.permission = r.get<int32_t>();
            p.health = r.get<float>(); p.maxHealth = r.get<float>(); p.absorption = r.get<float>(); p.hunger = r.get<float>();
            p.saturation = r.get<float>(); p.xpProgress = r.get<float>(); p.walkSpeed = r.get<float>();
            p.xpLevel = r.get<int32_t>(); p.air = r.get<int32_t>(); p.maxAir = r.get<int32_t>();
            return p;
        }
    };

    struct Effect {
        int32_t id = 0, amplifier = 0, ticks = 0;
        bool ambient = false, particles = true;
        void Write(Writer& w) const { w.put(id).put(amplifier).put(ticks).boolean(ambient).boolean(particles); }
        static Effect Read(Reader& r) {
            Effect e;
            e.id = r.get<int32_t>(); e.amplifier = r.get<int32_t>(); e.ticks = r.get<int32_t>();
            e.ambient = r.boolean(); e.particles = r.boolean();
            return e;
        }
    };

    struct Inventory {
        std::array<Item, 36> slots;
        std::array<Item, 4> armor;
        Item offhand, cursor;
        int32_t selected = 0;
        bool open = false;

        void Write(Writer& w) const {
            for (const Item& i : slots) i.Write(w);
            for (const Item& i : armor) i.Write(w);
            offhand.Write(w);
            cursor.Write(w);
            w.put(selected).boolean(open);
        }
        static Inventory Read(Reader& r) {
            Inventory inv;
            for (Item& i : inv.slots) i = Item::Read(r);
            for (Item& i : inv.armor) i = Item::Read(r);
            inv.offhand = Item::Read(r);
            inv.cursor = Item::Read(r);
            inv.selected = r.get<int32_t>();
            inv.open = r.boolean();
            return inv;
        }
    };

    struct Entity {
        uint64_t runtimeId = 0;
        int64_t uniqueId = 0;
        std::string type, name, nameTag, uuid;
        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0, headYaw = 0, health = -1, scale = 1;
        bool onGround = false, isPlayer = false, isItem = false, isSelf = false;
        uint64_t flags = 0;
        std::string itemName;
        int32_t itemCount = 0;
        std::string mainHand, offHand;
        std::array<std::string, 4> armor;
        int64_t vehicleUid = 0, ownerUid = 0, targetUid = 0;

        void Write(Writer& w) const {
            w.put(runtimeId).put(uniqueId).str(type).str(name).str(nameTag).str(uuid);
            w.put(x).put(y).put(z).put(yaw).put(pitch).put(headYaw).put(health).put(scale);
            w.boolean(onGround).boolean(isPlayer).boolean(isItem).boolean(isSelf);
            w.put(flags).str(itemName).put(itemCount).str(mainHand).str(offHand);
            for (const auto& a : armor) w.str(a);
            w.put(vehicleUid).put(ownerUid).put(targetUid);
        }
        static Entity Read(Reader& r) {
            Entity e;
            e.runtimeId = r.get<uint64_t>(); e.uniqueId = r.get<int64_t>();
            e.type = r.str(); e.name = r.str(); e.nameTag = r.str(); e.uuid = r.str();
            e.x = r.get<double>(); e.y = r.get<double>(); e.z = r.get<double>();
            e.yaw = r.get<float>(); e.pitch = r.get<float>(); e.headYaw = r.get<float>(); e.health = r.get<float>(); e.scale = r.get<float>();
            e.onGround = r.boolean(); e.isPlayer = r.boolean(); e.isItem = r.boolean(); e.isSelf = r.boolean();
            e.flags = r.get<uint64_t>(); e.itemName = r.str(); e.itemCount = r.get<int32_t>();
            e.mainHand = r.str(); e.offHand = r.str();
            for (auto& a : e.armor) a = r.str();
            e.vehicleUid = r.get<int64_t>(); e.ownerUid = r.get<int64_t>(); e.targetUid = r.get<int64_t>();
            return e;
        }
    };

    struct PlayerEntry {
        std::string uuid, name;
        int64_t uid = 0;
        int32_t permission = -1;
        void Write(Writer& w) const { w.str(uuid).str(name).put(uid).put(permission); }
        static PlayerEntry Read(Reader& r) {
            PlayerEntry p;
            p.uuid = r.str(); p.name = r.str(); p.uid = r.get<int64_t>(); p.permission = r.get<int32_t>();
            return p;
        }
    };

    struct WorldState {
        int32_t dimension = 0, difficulty = 0, worldGameMode = 0;
        int64_t day = 0;
        int32_t timeOfDay = 0;
        double totalTicks = 0;
        float rain = 0, thunder = 0;
        bool commandsEnabled = true, haveSpawn = false;
        int32_t spawnX = 0, spawnY = 0, spawnZ = 0;
        std::vector<std::pair<std::string, int32_t>> rules;

        void Write(Writer& w) const {
            w.put(dimension).put(difficulty).put(worldGameMode).put(day).put(timeOfDay).put(totalTicks).put(rain).put(thunder);
            w.boolean(commandsEnabled).boolean(haveSpawn).put(spawnX).put(spawnY).put(spawnZ);
            w.put<uint32_t>(static_cast<uint32_t>(rules.size()));
            for (const auto& rule : rules) w.str(rule.first).put(rule.second);
        }
        static WorldState Read(Reader& r) {
            WorldState s;
            s.dimension = r.get<int32_t>(); s.difficulty = r.get<int32_t>(); s.worldGameMode = r.get<int32_t>();
            s.day = r.get<int64_t>(); s.timeOfDay = r.get<int32_t>(); s.totalTicks = r.get<double>();
            s.rain = r.get<float>(); s.thunder = r.get<float>();
            s.commandsEnabled = r.boolean(); s.haveSpawn = r.boolean();
            s.spawnX = r.get<int32_t>(); s.spawnY = r.get<int32_t>(); s.spawnZ = r.get<int32_t>();
            const uint32_t n = r.get<uint32_t>();
            for (uint32_t i = 0; i < n && r.Ok(); i++) {
                std::string k = r.str();
                s.rules.emplace_back(std::move(k), r.get<int32_t>());
            }
            return s;
        }
    };

    struct BlockInfo {
        bool loaded = false;
        uint32_t runtimeId = 0;
        std::string name;
        std::vector<std::pair<std::string, std::string>> states;
        int32_t biome = 0;
        uint32_t liquid = 0;
        void Write(Writer& w) const {
            w.boolean(loaded).put(runtimeId).str(name).put(biome).put(liquid);
            w.put<uint32_t>(static_cast<uint32_t>(states.size()));
            for (const auto& s : states) w.str(s.first).str(s.second);
        }
        static BlockInfo Read(Reader& r) {
            BlockInfo b;
            b.loaded = r.boolean(); b.runtimeId = r.get<uint32_t>(); b.name = r.str(); b.biome = r.get<int32_t>(); b.liquid = r.get<uint32_t>();
            const uint32_t n = r.get<uint32_t>();
            for (uint32_t i = 0; i < n && r.Ok(); i++) {
                std::string k = r.str();
                b.states.emplace_back(std::move(k), r.str());
            }
            return b;
        }
    };

    struct RayHit {
        bool hit = false;
        int32_t x = 0, y = 0, z = 0, face = 0;
        double hx = 0, hy = 0, hz = 0, distance = 0;
        std::string block;
        void Write(Writer& w) const { w.boolean(hit).put(x).put(y).put(z).put(face).put(hx).put(hy).put(hz).put(distance).str(block); }
        static RayHit Read(Reader& r) {
            RayHit h;
            h.hit = r.boolean(); h.x = r.get<int32_t>(); h.y = r.get<int32_t>(); h.z = r.get<int32_t>(); h.face = r.get<int32_t>();
            h.hx = r.get<double>(); h.hy = r.get<double>(); h.hz = r.get<double>(); h.distance = r.get<double>(); h.block = r.str();
            return h;
        }
    };

    struct AimState {
        int32_t kind = 0;
        RayHit block;
        uint64_t entity = 0;
        std::string entityType;
        double distance = 0;
        void Write(Writer& w) const { w.put(kind); block.Write(w); w.put(entity).str(entityType).put(distance); }
        static AimState Read(Reader& r) {
            AimState a;
            a.kind = r.get<int32_t>(); a.block = RayHit::Read(r); a.entity = r.get<uint64_t>(); a.entityType = r.str(); a.distance = r.get<double>();
            return a;
        }
    };

    struct ScreenState {
        int32_t guiWidth = 0, guiHeight = 0, guiScale = 1, framebufferWidth = 0, framebufferHeight = 0;
        float fps = 0, frameMs = 0;
        Screen screen = Screen::None;
        bool paused = false, connected = false, hudHidden = false, debug = false;
        std::string server, language, platform, gameVersion;
        void Write(Writer& w) const {
            w.put(guiWidth).put(guiHeight).put(guiScale).put(framebufferWidth).put(framebufferHeight).put(fps).put(frameMs);
            w.put(static_cast<int32_t>(screen)).boolean(paused).boolean(connected).boolean(hudHidden).boolean(debug);
            w.str(server).str(language).str(platform).str(gameVersion);
        }
        static ScreenState Read(Reader& r) {
            ScreenState s;
            s.guiWidth = r.get<int32_t>(); s.guiHeight = r.get<int32_t>(); s.guiScale = r.get<int32_t>();
            s.framebufferWidth = r.get<int32_t>(); s.framebufferHeight = r.get<int32_t>();
            s.fps = r.get<float>(); s.frameMs = r.get<float>();
            s.screen = static_cast<Screen>(r.get<int32_t>());
            s.paused = r.boolean(); s.connected = r.boolean(); s.hudHidden = r.boolean(); s.debug = r.boolean();
            s.server = r.str(); s.language = r.str(); s.platform = r.str(); s.gameVersion = r.str();
            return s;
        }
    };

    struct Meta {
        uint32_t apiVersion = kApiVersion;
        std::string gameVersion, plugin, platform;
        uint32_t permissions = 0;
        int64_t nowMs = 0;
        uint64_t tick = 0;
        void Write(Writer& w) const { w.put(apiVersion).str(gameVersion).str(plugin).str(platform).put(permissions).put(nowMs).put(tick); }
        static Meta Read(Reader& r) {
            Meta m;
            m.apiVersion = r.get<uint32_t>(); m.gameVersion = r.str(); m.plugin = r.str(); m.platform = r.str();
            m.permissions = r.get<uint32_t>(); m.nowMs = r.get<int64_t>(); m.tick = r.get<uint64_t>();
            return m;
        }
    };

    struct InputOverrideState {
        float forward = 0, strafe = 0;
        bool jump = false, sneak = false, sprint = false, attack = false, use = false;
        int32_t ticks = 1;
        void Write(Writer& w) const { w.put(forward).put(strafe).boolean(jump).boolean(sneak).boolean(sprint).boolean(attack).boolean(use).put(ticks); }
        static InputOverrideState Read(Reader& r) {
            InputOverrideState s;
            s.forward = r.get<float>(); s.strafe = r.get<float>();
            s.jump = r.boolean(); s.sneak = r.boolean(); s.sprint = r.boolean(); s.attack = r.boolean(); s.use = r.boolean();
            s.ticks = r.get<int32_t>();
            return s;
        }
    };

    struct EventTick {
        float dt = 0.05f;
        uint64_t tick = 0;
        void Write(Writer& w) const { w.put(dt).put(tick); }
        static EventTick Read(Reader& r) { EventTick e; e.dt = r.get<float>(); e.tick = r.get<uint64_t>(); return e; }
    };
    struct EventFrame {
        float dt = 0, fps = 0;
        int32_t guiWidth = 0, guiHeight = 0;
        void Write(Writer& w) const { w.put(dt).put(fps).put(guiWidth).put(guiHeight); }
        static EventFrame Read(Reader& r) {
            EventFrame e;
            e.dt = r.get<float>(); e.fps = r.get<float>(); e.guiWidth = r.get<int32_t>(); e.guiHeight = r.get<int32_t>();
            return e;
        }
    };
    struct EventChat {
        int32_t type = 0;
        std::string sender, message, raw;
        void Write(Writer& w) const { w.put(type).str(sender).str(message).str(raw); }
        static EventChat Read(Reader& r) {
            EventChat e;
            e.type = r.get<int32_t>(); e.sender = r.str(); e.message = r.str(); e.raw = r.str();
            return e;
        }
    };
    struct EventKey {
        std::string id;
        bool down = false;
        void Write(Writer& w) const { w.str(id).boolean(down); }
        static EventKey Read(Reader& r) { EventKey e; e.id = r.str(); e.down = r.boolean(); return e; }
    };
    struct EventCommand {
        std::string name, args;
        void Write(Writer& w) const { w.str(name).str(args); }
        static EventCommand Read(Reader& r) { EventCommand e; e.name = r.str(); e.args = r.str(); return e; }
    };
    struct EventConnection {
        std::string server, message;
        int32_t reason = 0;
        void Write(Writer& w) const { w.str(server).str(message).put(reason); }
        static EventConnection Read(Reader& r) { EventConnection e; e.server = r.str(); e.message = r.str(); e.reason = r.get<int32_t>(); return e; }
    };
    struct EventDamage {
        bool known = false;
        float dx = 0, dz = 0, health = 0;
        void Write(Writer& w) const { w.boolean(known).put(dx).put(dz).put(health); }
        static EventDamage Read(Reader& r) { EventDamage e; e.known = r.boolean(); e.dx = r.get<float>(); e.dz = r.get<float>(); e.health = r.get<float>(); return e; }
    };
    struct EventText {
        std::string text;
        void Write(Writer& w) const { w.str(text); }
        static EventText Read(Reader& r) { EventText e; e.text = r.str(); return e; }
    };
    struct EventChange {
        int32_t from = 0, to = 0;
        void Write(Writer& w) const { w.put(from).put(to); }
        static EventChange Read(Reader& r) { EventChange e; e.from = r.get<int32_t>(); e.to = r.get<int32_t>(); return e; }
    };
    struct EventSlot {
        int32_t slot = 0;
        Item item;
        void Write(Writer& w) const { w.put(slot); item.Write(w); }
        static EventSlot Read(Reader& r) { EventSlot e; e.slot = r.get<int32_t>(); e.item = Item::Read(r); return e; }
    };
    struct EventEntity {
        Entity entity;
        void Write(Writer& w) const { entity.Write(w); }
        static EventEntity Read(Reader& r) { EventEntity e; e.entity = Entity::Read(r); return e; }
    };
    struct EventSound {
        std::string name;
        double x = 0, y = 0, z = 0;
        float volume = 1, pitch = 1;
        void Write(Writer& w) const { w.str(name).put(x).put(y).put(z).put(volume).put(pitch); }
        static EventSound Read(Reader& r) {
            EventSound e;
            e.name = r.str(); e.x = r.get<double>(); e.y = r.get<double>(); e.z = r.get<double>();
            e.volume = r.get<float>(); e.pitch = r.get<float>();
            return e;
        }
    };
    struct EventHttp {
        uint32_t id = 0;
        int32_t status = 0;
        std::string error;
        std::vector<std::pair<std::string, std::string>> headers;
        std::vector<uint8_t> body;
        void Write(Writer& w) const {
            w.put(id).put(status).str(error);
            w.put<uint32_t>(static_cast<uint32_t>(headers.size()));
            for (const auto& h : headers) w.str(h.first).str(h.second);
            w.bytes(body.data(), body.size());
        }
        static EventHttp Read(Reader& r) {
            EventHttp e;
            e.id = r.get<uint32_t>(); e.status = r.get<int32_t>(); e.error = r.str();
            const uint32_t n = r.get<uint32_t>();
            for (uint32_t i = 0; i < n && r.Ok(); i++) {
                std::string k = r.str();
                e.headers.emplace_back(std::move(k), r.str());
            }
            e.body = r.bytes();
            return e;
        }
    };
    struct EventPacket {
        uint32_t id = 0;
        bool outgoing = false;
        std::vector<uint8_t> body;
        void Write(Writer& w) const { w.put(id).boolean(outgoing).bytes(body.data(), body.size()); }
        static EventPacket Read(Reader& r) { EventPacket e; e.id = r.get<uint32_t>(); e.outgoing = r.boolean(); e.body = r.bytes(); return e; }
    };
    struct EventTitle {
        std::string title, subtitle, actionBar;
        void Write(Writer& w) const { w.str(title).str(subtitle).str(actionBar); }
        static EventTitle Read(Reader& r) { EventTitle e; e.title = r.str(); e.subtitle = r.str(); e.actionBar = r.str(); return e; }
    };

    struct Region {
        int32_t x0 = 0, y0 = 0, z0 = 0, sx = 0, sy = 0, sz = 0;
        std::vector<std::string> palette;
        std::vector<uint16_t> cells;

        const std::string& at(int x, int y, int z) const {
            static const std::string empty;
            if (x < x0 || y < y0 || z < z0 || x >= x0 + sx || y >= y0 + sy || z >= z0 + sz)
                return empty;
            const size_t i = (static_cast<size_t>(y - y0) * static_cast<size_t>(sz) + static_cast<size_t>(z - z0)) * static_cast<size_t>(sx) +
                             static_cast<size_t>(x - x0);
            return cells[i] < palette.size() ? palette[cells[i]] : empty;
        }
        void Write(Writer& w) const {
            w.put(x0).put(y0).put(z0).put(sx).put(sy).put(sz);
            w.put<uint32_t>(static_cast<uint32_t>(palette.size()));
            for (const auto& p : palette) w.str(p);
            w.put<uint32_t>(static_cast<uint32_t>(cells.size()));
            w.raw(cells.data(), cells.size() * sizeof(uint16_t));
        }
        static Region Read(Reader& r) {
            Region g;
            g.x0 = r.get<int32_t>(); g.y0 = r.get<int32_t>(); g.z0 = r.get<int32_t>();
            g.sx = r.get<int32_t>(); g.sy = r.get<int32_t>(); g.sz = r.get<int32_t>();
            const uint32_t np = r.get<uint32_t>();
            for (uint32_t i = 0; i < np && r.Ok(); i++) g.palette.push_back(r.str());
            const uint32_t nc = r.get<uint32_t>();
            if (r.Remaining() < static_cast<size_t>(nc) * 2) return g;
            g.cells.resize(nc);
            std::memcpy(g.cells.data(), r.Cursor(), static_cast<size_t>(nc) * 2);
            r.Skip(static_cast<size_t>(nc) * 2);
            return g;
        }
    };

    struct FoundBlock {
        int32_t x = 0, y = 0, z = 0;
        std::string name;
        void Write(Writer& w) const { w.put(x).put(y).put(z).str(name); }
        static FoundBlock Read(Reader& r) {
            FoundBlock b;
            b.x = r.get<int32_t>(); b.y = r.get<int32_t>(); b.z = r.get<int32_t>(); b.name = r.str();
            return b;
        }
    };

    inline constexpr uint32_t kEventCancel = 1;
}

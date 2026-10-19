# Perlin plugins

Plugins are WebAssembly modules written in modern C++ (C++23). They run inside a sandbox in the client, can see almost everything the game knows, and can draw on the HUD, add commands and key binds, and react to game events.

Drop `name.wasm` (and optionally `name.json` and `name.png`) into `<data dir>/mods/`. Plugins hot-reload when the file changes (Options → Hot Reload On Save) and can be enabled, disabled and reloaded from **Pause → Mods**.

## Quick start

```cpp
#include <perlin/perlin.hpp>

using namespace perlin;

PERLIN_PLUGIN(ctx) {
    // The lambda's parameters are the command's arguments: `/here`, `/tp 10 64 -3`, `/mode creative`.
    ctx.command<"here">("prints your block position", [] {
        if (auto p = game::player())
            (void)act::message("you are at {}", BlockPos(game::position(*p)));
    });

    ctx.on<event::ChatReceived>([](const abi::EventChat& e) {
        if (e.message.contains("creeper"))
            (void)act::toast("someone said creeper");
    });

    ctx.hud("clock", [](ui::Canvas& g) {
        g.text(4, 4, "hello from a plugin", {.color = Rgba::gold});
    });
}
```

Plugins are 64-bit WebAssembly (memory64) by default. Build one with Emscripten (<https://emscripten.org>, activate emsdk first):

```
source /path/to/emsdk/emsdk_env.sh
mods/build.sh my_plugin.cpp
```

`build.sh` compiles with `-std=c++23 -fno-exceptions -fno-rtti -sMEMORY64` as a standalone WASI reactor (no JavaScript) and writes `prebuilt/my_plugin.wasm` (the second argument picks another output directory).

Classic 32-bit plugins still work everywhere. `mods/build.sh --wasm32 my_plugin.cpp` builds one with a wasi-sdk install (<https://github.com/WebAssembly/wasi-sdk>, version 25 or newer, `WASI_SDK_PATH`); wasi-sdk has no 64-bit target. Any compiler that targets `wasm64` or `wasm32-wasip1` and exports `perlin_alloc`, `perlin_free`, `perlin_event` and `perlin_subscriptions` works; the headers in `mods/sdk/perlin` provide those exports and compile unchanged for both.

Perlin runs plugins on [WasmEdge](https://wasmedge.org) (desktop and Android) and on wasm3 in the browser build; both take 32-bit and 64-bit modules, and the Mods screen's log line says which one loaded (`speedometer: wasm64 on WasmEdge 0.18.0`).

## Modern C++ API

The SDK leans on the compiler instead of strings:

* **Compile-time reflection.** Every wire struct is an aggregate; its encoding is derived from the member list (`perlin/meta.hpp`: enumerator names, aggregate arity and field visitation, `fixed_string`). With `-DPERLIN_USE_STD_META` on a compiler that implements P2996 it uses real `std::meta`; otherwise a portable fallback gives the same results on GCC 13+ and Clang 18+.
* **Typed events.** `ctx.on<event::Tick>(...)` and `ctx.on<abi::Event::Tick>(...)` hand the handler the payload type from the ABI table (`abi::payload_t<E>`). Handlers may also take no argument and may return `Flow`.
* **Typed commands.** `ctx.command<"tp">("teleport", [](int x, int y, int z) {...})`. Parameter types are the grammar: text, integers, floats, bool, enumerations (matched by name, case-insensitive) and `std::optional<T>` for optional trailing arguments. The usage line (`/tp <integer> <integer> <integer>`, `/mode [Fast|Slow]`) is generated and shown on a bad call. `(const CommandCall&)` gives raw access.
* **Typed options and controls.** `game::option<game::Option::fov>()` returns `Result<double>`; `act::set_option<game::Option::vsync>(true)` is checked at compile time. `game::input_down(game::Control::Jump)`.
* **`Result<T>`** is `std::expected<T, Error>` (a small stand-in is used on standard libraries that lack it); `Error::message()` and `Error::name()` come from the `Status` enumerators.
* **Chrono timers.** `ctx.after(2s, fn)`, `ctx.every(20_ticks, fn)` return a `TimerId`.
* **Flags and sets.** `Permissions` is `meta::Flags<Permission>`; entity flags are a `meta::IndexSet<EntityFlag>`. `std::format("{}", abi::Screen::Mods)` prints `Mods`.

## Settings

A mod declares settings once; the game lists them on the mod's **Settings** page (Mods screen, pick the mod, **Settings**), saves them to `<data dir>/mods/<name>.settings.json`, and tells the mod when one changes, live.

```cpp
enum class Unit { BlocksPerSecond, KilometersPerHour };

PERLIN_PLUGIN(ctx) {
    static auto shown = ctx.toggle("shown", "Show speedometer", true, {.description = "Draws your speed.", .icon = icon::item("minecraft:clock")});
    static auto unit  = ctx.choice("unit", "Unit", Unit::BlocksPerSecond);        // choices are the enumerators
    static auto warn  = ctx.number("warn", "Highlight above", 8.0, 1.0, 40.0, 0.5, {.unit = "b/s"});
    static auto smooth = ctx.integer("smooth", "Smoothing", 4, 1, 20);
    ctx.button("reset", "Reset peak", [] { /* pressed */ });
    ctx.header("about", "About");
    ctx.note("version", "Version 1.2");

    ctx.on_setting_changed([](const abi::EventSettingChanged& e) { /* e.id, e.value */ });
    // shown.get(), unit.get(), ... always hold the current value. shown.set(false) changes it from the mod side.
}
```

`SettingOptions` has `description` (shown as a tooltip), `icon`, `unit` and `restart` (reload the mod when the page closes). `ctx.open_settings()` opens the page.

## Icons

* Mod icon: `name.png` next to the `.wasm`, `"icon"` in the manifest, `<name>.data/pack_icon.png` (the same file name resource packs use), or `set_mod_icon("icons/me.png")` at runtime.
* Setting icons (`SettingOptions::icon`): `icon::item("minecraft:feather")` draws an item, `icon::texture("textures/items/apple")` a resource-pack texture (so a resource pack can restyle them), `icon::file("icons/speed.png")` a PNG from the mod's data folder.

## Intents

Intents are Android-style messages: an **action**, an optional **target** mod, and typed **extras**. They are how mods drive the game, and each other, without sharing code.

```cpp
// Ask the game to do something.
ctx.send(Intent(abi::HostAction::OpenScreen).put("screen", Screen::Mods));
ctx.send(Intent(abi::HostAction::CopyText).put("text", "hello"), [](const IntentResult& r) { /* r.outcome, r.handler, r.extras */ });

// Handle an action (yours, or one another mod defines).
ctx.on_intent<"speedometer.action.SET_UNIT">("Changes the unit", [](const IntentCall& call) {
    if (auto u = call.get<Unit>("unit")) unit.set(*u);
    (void)call.reply(std::pair{"ok", true});
});

// Send to a specific mod.
ctx.send(Intent("speedometer.action.SET_UNIT").to("speedometer.wasm").put("unit", Unit::KilometersPerHour));
```

Extras are `bool`, integers (enumerations travel as integers), floating point, text and bytes. Game actions (`abi::HostAction`; the permission-gated ones follow the table above): `OpenScreen`, `OpenModSettings`, `Screenshot`, `SetPerspective`, `CyclePerspective`, `CopyText`, `ReloadResources`, `OpenUrl` (needs `network`; https only) and `ToggleHud`, `CopyFile`, `MoveFile` and `DownloadFile` (`ctx.copy_file`, `ctx.move_file`, `ctx.download`; paths are `file_spec(Area::ResourcePacks, "pack.zip")`). Their wire names are derived from the enumerators at compile time (`perlin.action.OPEN_SCREEN`). `intent_handlers("action")` lists who would handle an action. Mod-to-mod intents are queued and delivered on the next frame, so a handler never runs inside another mod's event.

Prebuilt examples live in `mods/prebuilt`: `speedometer`, `inspector` and `radar`, with sources in `mods/examples`.

## Any language

The C++ SDK is a convenience. A plugin is any WebAssembly module (core wasm, no component model) that targets a WASI preview 1 or freestanding environment, so Rust (`wasm32-wasip1`), C, Zig, Go (TinyGo), AssemblyScript, Swift, Nim and anything else that emits wasm work. The contract is small:

* Import one function: module `perlin`, name `call`, signature `(op: i32, in: ptr, in_len: i32, out: ptr, out_cap: i32) -> i32`.
* Export `perlin_alloc(size: i32) -> ptr`, `perlin_free(ptr)`, `perlin_event(id: i32, ptr, len: i32) -> i32` and `perlin_subscriptions() -> i32`.
* `ptr` is `i32` in a 32-bit module and `i64` in a 64-bit (memory64) one: exactly what `void*` becomes when you compile for `wasm32` or `wasm64`. The host reads the module's declared types, so it also accepts `i64` lengths (`size_t`) in a 64-bit plugin.
* Linear memory must be the module's own (not imported) and exported; 1 GiB at most.
* `perlin_subscriptions` returns a bitmask with bit `1 << event_id` set for every event you want delivered (see `Event` in `mods/sdk/perlin/abi.hpp`; id 1 is Load, 7 is Connected, 3 is Tick, 4 is Frame). `perlin_event` returns bit 0 set to cancel a cancellable event.
* The host calls `perlin_event(Load = 1, ...)` once after instantiation; register everything (commands, keys, HUD) from there.
* Payloads and arguments are little-endian packed values: integers and floats at their natural width, strings and byte blobs as a `u32` length followed by the bytes, booleans as one byte.
* `call` returns the number of bytes written to `out` (>= 0), a negative `Status` code (`-1` unknown op, `-2` denied, `-3` bad arguments, `-4` unavailable, `-5` not found, `-6` failed), or a value below `-16` meaning the buffer was too small: call again with `out_cap` of `-rc - 16`.
* Operation ids are the `Op` enum in `abi.hpp`; each C++ wrapper in `game.hpp`, `act.hpp`, `ui.hpp` and `io.hpp` shows the exact argument layout for that op.

`mods/examples/c/hello.c` is a complete plugin in plain C (about 30 lines, no SDK) that prints a message when you join a server; build it with `mods/examples/c/build.sh` (`--wasm32` for wasi-sdk).

## Manifest

`name.json` next to the `.wasm` is optional:

```json
{
  "name": "Speedometer",
  "description": "Shows your speed.",
  "version": "1.0.0",
  "author": "You",
  "website": "https://example.com",
  "permissions": ["network", "files", "packets", "control"]
}
```

`name.png` is the icon in the Mods screen.

## Permissions

The API exposes everything the game knows and can do. Only the dangerous operations are gated: those that act on your behalf or leave your machine have to be declared in the manifest and are shown in the Mods screen. Everything else (reading state, drawing, local messages, settings, intents between mods, storage in the plugin's own folder) is always allowed.

| Permission | Allows |
| --- | --- |
| *(always)* | player, inventory, world, blocks, entities, player list, options (read), screen and input state, translations, HUD drawing, local messages, toasts, titles, sounds, commands, key binds, storage |
| `control` | sending chat and commands, hotbar and inventory actions, swinging, using items, attacking, interacting, breaking blocks, looking, driving movement inputs, jumping, respawning, leaving the game, changing options |
| `network` | HTTPS requests, `OpenUrl`, downloads |
| `files` | `CopyFile`, `MoveFile` and (with `network`) `DownloadFile` intents between the mod's own folder and the game's screenshot, resource-pack and shader-pack folders; never overwrites a game file |
| `packets` | sending raw protocol packets (login, handshake, resource pack and disconnect packets are refused) |
| `storage` | always granted; reads and writes under `<data dir>/mods/<name>.data/` only |

An operation without its permission returns `Error::denied()` instead of running.

**Approval.** A mod that asks for `network`, `files`, `control` or `packets` is never enabled just because it was dropped into the folder. It shows as *Needs approval* in the Mods screen, with a plain-language list of what it asks to do, and starts only after you press **Allow & Enable**. The approval covers exactly the permissions it asked for; a later update that asks for more switches it off again until you approve it. Mods that only use the always-allowed operations start right away.

## Sandbox

* Plugins have no file, socket or process access of their own; of WASI only stdout/stderr (to the log), clocks and random numbers work. Everything goes through the host calls in `perlin/*.hpp`.
* Storage paths are relative to the plugin's own folder; `..`, absolute paths and backslashes are rejected.
* HTTP is HTTPS only, never follows redirects, blocks loopback, private and link-local addresses, caps responses at 4 MiB and allows six requests at a time.
* The account file (`auth.json`) and its tokens are never reachable. `game::options_json()` lists only a fixed set of settings, and `playerName`, `server`, `texturePack` and `shaderPack` are read-only.
* Each plugin runs on its own thread, one event at a time. An event that takes longer than 250 ms (4 s for loading) revokes the plugin: its host calls stop working, its commands, keys and HUD layers are removed, and the Mods screen shows it as unresponsive until you reload it. A plugin stuck in a loop is interrupted on the spot (WasmEdge); in the browser build the interpreter can't be interrupted, so it keeps one thread busy until the page closes.

## API tour

All calls return `Result<T>` (`std::expected<T, Error>`); `Error::status` is one of `UnknownOp`, `Denied`, `BadArguments`, `Unavailable` (for example no server connection), `NotFound` or `Failed`.

### `game::` (read state)

| Call | Returns |
| --- | --- |
| `player()` | position, velocity, look, flags, health, hunger, xp, air, permission level, game mode, dimension |
| `effects()`, `inventory()` | status effects, 36 slots + armor + offhand + cursor |
| `world()` | dimension, difficulty, game rules, time of day, day count, weather, spawn |
| `block(pos)` | name, block states, biome, liquid, loaded flag |
| `region(a, b)` | up to 262 144 blocks as a palette + cells (`Region::at(x, y, z)`) |
| `raycast(origin, dir, {max_distance, include_liquids})`, `aim()` | block hit; what the crosshair targets (block or entity) |
| `find_blocks(center, {name, radius, max_results, exact})` | nearest matches first, radius up to 32 |
| `chunk_loaded(cx, cz)` | whether the chunk is in memory |
| `entities({radius, type, players_only, include_items})`, `entity(id)`, `players()` | nearby entities, one entity, the tab list |
| `screen()` | GUI size and scale, fps, which screen is open, platform, server, language |
| `options_json()`, `option<Option::fov>()`, `option_text(key)` | settings |
| `perspective()`, `chat_history(n)`, `title_state()`, `resource_packs()`, `mods()`, `block_names()` | camera mode, recent chat lines, current title/subtitle/action bar, active packs, installed mods, every block name |
| `translate(key)` | localized string |
| `server_commands()`, `input_actions()`, `input_down(action)`, `mouse()` | server command names, action ids, held state of an action or `key.keyboard.x`, cursor in GUI pixels |

### `act::` (do things)

Free: `message`, `toast`, `title`, `play` (sound). Needs `control`: `say`, `run_command`, `select_slot`, `swing`, `use_item`, `use_on`, `attack`, `interact`, `break_block`, `look`, `look_at`, `jump`, `respawn`, `disconnect`, `drop`, `swap_slots`, `click_slot`, `open_inventory`, `close_inventory`, `drive(Inputs)`, `set_option`, `set_perspective`, `reload_resources`.

`drive` holds movement inputs for a number of ticks (`forward > 0` walks forward, `strafe > 0` goes right); call it every tick to keep moving. `look` takes the same yaw and pitch that `PlayerState` reports (yaw 0 faces south, pitch positive looks down). `break_block` breaks instantly in creative; in survival it starts mining, so keep the crosshair on the block and `drive` with `attack = true` until it breaks.

### `ui::` (HUD)

`ctx.hud("name", [](ui::Canvas& g) { ... }, z)` redraws every frame and only sends the layer to the game when the drawing changed. `Canvas` has `rect`, `gradient`, `border`, `line`, `text` (with `TextStyle{.color, .scale, .shadow, .align}`), `item` (an item icon by id) and `image` (a resource-pack texture path). Coordinates are GUI pixels (the same units as the vanilla HUD); `g.width()` and `g.height()` are the current GUI size. `ui::text_width(text, scale)` measures with the game's font. Layers are hidden when the HUD is hidden or a menu is open.

### `io::` (`fs`, `Config`, `net`)

`fs::read_text/read_bytes/write_text/write_bytes/list/exists/remove/make_directory` work inside the plugin's data folder. `Config("settings.cfg")` is a small `key=value` store with typed `get`/`set` and `save()`. `net::get/post/send` take a callback that runs on a later frame with the `Response`. `net::send_packet(id, bytes)` and `net::packet_name(id)` are the raw packet escape hatch.

### Events

Register with `ctx.on<event::Name>([](const event::Name::Payload& e) { ... })`. Handlers returning `Flow::cancel` cancel cancellable events.

| Event | Payload | Cancellable |
| --- | --- | --- |
| `Tick` | `dt`, `tick` (20 per second) | |
| `Frame` | `dt`, `fps`, GUI size | |
| `ChatReceived` | `message` | yes (hides the line) |
| `ChatSending` | `text` | yes (nothing is sent) |
| `Command` | handled for you by `ctx.command` | |
| `Key` | handled for you by `ctx.bind` | |
| `Connected`, `Disconnected` | `server`, `message`, `reason` | |
| `Damage` | direction, health | |
| `Death`, `Respawned` | cause | |
| `DimensionChanged`, `ScreenChanged` | `from`, `to` | |
| `SlotChanged` | `slot`, `item` | |
| `EntityAdded`, `EntityRemoved` | entity (removed events only fill the id) | |
| `Sound` | name, position, volume, pitch | |
| `Title` | title, subtitle, action bar | |
| `Packet` | id and body of every packet received from the server | |
| `HttpResponse` | handled for you by `net::send` | |
| `SettingChanged` | `id`, `value`, `kind` (also updates `Setting` handles) | |
| `Intent`, `IntentResult` | handled for you by `ctx.on_intent` / `ctx.send` | |

`ctx.after(delay, fn)` and `ctx.every(period, fn)` take any `std::chrono` duration (`20_ticks` too), run on the tick clock and return a `TimerId` for `ctx.cancel(id)`. `ctx.on_unload(fn)` runs before a reload.

### Commands and keys

`ctx.command<"name">("description", fn)` adds a client-side slash command that appears in chat completion (see *Typed commands*); the string form `ctx.command("name", "/name <arg>", "description", fn)` still works. `ctx.bind<"id">("Label", Key::G, fn)` (or `ctx.bind("id", "Label", Key::G, fn)`) registers a key that is active while no menu or chat is open. Players rebind plugin keys in **Options → Controls → Key Binds** (a section per plugin); the choice is stored in `<data dir>/mods/<name>.keys.json` (`{"id": "key.keyboard.g"}`).

## Writing against another game version

`game::meta()` returns the game version, platform, the plugin's permissions and the host API version (`abi::kApiVersion`). Operations a game version does not provide return `Error::status == UnknownOp`, so plugins written against newer headers degrade instead of crashing.

## Layout

```
mods/
  build.sh            build script (wasm64 with Emscripten; --wasm32 with wasi-sdk)
  sdk/perlin/         headers: perlin.hpp pulls in everything
    abi.hpp           wire format shared with the host
    rpc.hpp           Result/Error and the single host import
    meta.hpp          compile-time reflection (enum names, aggregates, flags, fixed_string)
    detail/           field-binding helpers used by meta.hpp
    game.hpp act.hpp ui.hpp io.hpp types.hpp runtime.hpp
  examples/           source for the prebuilt plugins
  tests/              meta_test, abi_test (wire format), sdk_test (fake host); build each with g++ -std=c++23 -Isdk
  prebuilt/           built .wasm files and their manifests
```

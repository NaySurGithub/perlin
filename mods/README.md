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

Perlin runs plugins on [WasmEdge](https://wasmedge.org) (desktop and Android) and on wasm3 in the browser build; both take 32-bit and 64-bit modules, and the Mods screen's log line says which one loaded (`waila: wasm64 on WasmEdge 0.18.0`).

## Modern C++ API

The SDK leans on the compiler instead of strings:

* **Compile-time reflection.** Every wire struct is an aggregate; its encoding is derived from the member list (`perlin/meta.hpp`: enumerator names, aggregate arity and field visitation, `fixed_string`). With `-DPERLIN_USE_STD_META` on a compiler that implements P2996 it uses real `std::meta`; otherwise a portable fallback gives the same results on GCC 13+ and Clang 18+.
* **Typed events.** `ctx.on<event::Tick>(...)` and `ctx.on<abi::Event::Tick>(...)` hand the handler the payload type from the ABI table (`abi::payload_t<E>`). Handlers may also take no argument and may return `Flow`.
* **Typed commands.** `ctx.command<"tp">("teleport", [](int x, int y, int z) {...})`. Parameter types are the grammar: text, integers, floats, bool, enumerations (matched by name, case-insensitive) and `std::optional<T>` for optional trailing arguments. The usage line (`/tp <integer> <integer> <integer>`, `/mode [Fast|Slow]`) is generated and shown on a bad call. `(const CommandCall&)` gives raw access.
* **Typed options and controls.** `game::option<game::Option::fov>()` returns `Result<double>`; `act::set_option<game::Option::vsync>(true)` is checked at compile time. `game::input_down(game::Control::Jump)`.
* **`Result<T>`** is `std::expected<T, Error>` (a small stand-in is used on standard libraries that lack it); `Error::message()` and `Error::name()` come from the `Status` enumerators.
* **Chrono timers.** `ctx.after(2s, fn)`, `ctx.every(20_ticks, fn)` return a `TimerId`.
* **Flags and sets.** `Permissions` is `meta::Flags<Permission>`; entity flags are a `meta::IndexSet<EntityFlag>`. `std::format("{}", abi::Screen::Mods)` prints `Mods`.

## Everything is exposed

The wire format is derived from the member list of each struct (`abi.hpp`), so adding a field to a struct the host already sends, such as an entity's attributes, exposes it to every plugin and every language with no wrapper to write. `tools/check_api.py` (run by CTest as `mod_api_check`) fails when an operation is declared without a host or without an SDK wrapper, or an event is never delivered. Every enumeration in `perlin::abi` prints by name with `std::format` without being listed anywhere. `perlin/json.hpp` is a small JSON reader for the text the game hands over (block entity tags, option dumps, HTTP bodies): `json::parse(text)` then `tag["Items"][0]["Name"].string()`; missing keys read as null, so chains never fail.

## Settings

Settings are written in the mod's JSON, not in code. The game shows them on the mod's **Settings** page (Mods screen, pick the mod, **Settings**), stores them in `<data dir>/mods/<name>.settings.json`, and keeps the mod up to date while it runs.

```json
"settings": [
  {"type": "header", "label": "Readout"},
  {"id": "visible", "type": "toggle", "label": "Show overlay", "default": true, "icon": {"item": "minecraft:clock"}},
  {"id": "unit", "type": "choice", "label": "Unit", "default": "BlocksPerSecond",
   "choices": [{"value": "BlocksPerSecond", "label": "Blocks per second"}, {"value": "KilometersPerHour", "label": "Kilometers per hour"}]},
  {"id": "warn_at", "type": "number", "label": "Highlight above", "default": 8, "min": 1, "max": 40, "step": 0.5, "unit": "b/s"},
  {"id": "smoothing", "type": "integer", "label": "Smoothing", "default": 4, "min": 1, "max": 20},
  {"id": "reset_peak", "type": "button", "label": "Reset peak"}
]
```

| `type` | C++ handle | Value |
| --- | --- | --- |
| `toggle` | `Toggle` | `bool` |
| `integer` | `Integer` | `int` (`min`, `max`, `step`) |
| `number` | `Number` | `double` (`min`, `max`, `step`, `unit`) |
| `choice` | `Choice<E>` | an enumeration generated from `choices` |
| `button` | `Button` | no value; handle presses with `ctx.on_press` |
| `header`, `label` | | layout only |

Every setting also takes `label`, `description`, `restart` (needs a restart to apply) and `icon`: `{"item": "minecraft:clock"}` draws an item, `{"texture": "textures/items/apple"}` a resource-pack texture (so a pack can restyle it), `{"file": "icons/speed.png"}` a PNG from the mod's folder. Choice values are C++ identifiers; they become the enumerators.

`build.sh` reads `name.json` next to `name.cpp` (headers next to the source are found with `#include <dir/header.hpp>`) and generates `mod_settings.hpp` with one typed handle per setting. Use it like a plain value:

```cpp
#include <mod_settings.hpp>

if (settings::visible) { ... }                                   // bool
const double limit = settings::warn_at;                          // double
if (settings::unit == settings::Unit::KilometersPerHour) { ... }  // generated enum
(void)settings::visible.set(false);                              // the settings page shows it too

ctx.on_change(settings::unit, [](settings::Unit unit) { ... });  // fires for changes from the page or from set()
ctx.on_press(settings::reset_peak, [] { ... });
```

Without the generated header (another build system, another language) the same values are `perlin::setting<double>("warn_at")`. The generator is `mods/tools/perlin_settings.py`.

## Game actions

Mods call the game directly with typed functions and enums; there are no messages to compose.

```cpp
act::open_screen(Screen::Mods);        // Pause, Options, Mods, Wardrobe
act::open_settings();                  // this mod's settings page (or open_settings("other.wasm"))
act::screenshot();
act::cycle_perspective();
act::toggle_hud();
act::copy_to_clipboard("hello");
act::open_url("https://example.com");  // needs `network`

fs::copy(fs::in(Area::Screenshots, "a.png"), fs::in(Area::Mod, "keep/a.png"));   // needs `files`
fs::move(fs::in(Area::Mod, "pack.zip"), fs::in(Area::ResourcePacks, "pack.zip"));
net::download("https://example.com/pack.zip", fs::in(Area::ResourcePacks, "pack.zip"), [](Result<void> r) { ... });  // `network` and `files`
```

`Area` is `Mod`, `Screenshots`, `ResourcePacks` or `ShaderPacks`. Copies and moves never overwrite a game file.

## WAILA

`mods/waila` is a full plugin built on this API: a tooltip for the block or entity under your crosshair, styled after Jade (icon, name, mod name in italic blue, harvest tool with a check mark, breaking progress, crop growth, redstone, health hearts, pet owner, horse stats, container contents). Its source is the best example of the API, and it has an extension API of its own; see `mods/waila/README.md`. The prebuilt module is `mods/prebuilt/waila.wasm` with `waila.json` and `waila.png`; drop the three into `<data dir>/mods/`.

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

`name.json` next to the `.wasm` is optional (it also holds the `settings`, see above):

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

The API exposes everything the game knows and can do. Only the dangerous operations are gated: those that act on your behalf or leave your machine have to be declared in the manifest and are shown in the Mods screen. Everything else (reading state, drawing, local messages, settings, storage in the plugin's own folder) is always allowed.

| Permission | Allows |
| --- | --- |
| *(always)* | player, inventory, world, blocks, entities, player list, options (read), screen and input state, translations, HUD drawing, local messages, toasts, titles, sounds, commands, key binds, storage |
| `control` | sending chat and commands, hotbar and inventory actions, swinging, using items, attacking, interacting, breaking blocks, looking, driving movement inputs, jumping, respawning, leaving the game, changing options |
| `network` | HTTPS requests, `act::open_url`, downloads |
| `files` | `fs::copy`, `fs::move` and (with `network`) `net::download` between the mod's own folder and the game's screenshot, resource-pack and shader-pack folders; never overwrites a game file |
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
| `target()` | what the crosshair is on, fully resolved: a `BlockTarget` (position, face, block with states, `props`, display name, `entity()`) or an `EntityTarget` (all entity data, display name) or nothing |
| `block(pos)` | name, block states (`state_int("age")`, `state_bool(...)`), biome, liquid, loaded flag |
| `block_props(name)` | hardness, mining tool, harvest level, display name, block properties |
| `block_entity(pos)` | sign text, banner patterns, waxed, and the whole tag as JSON in `nbt` (read it with `json::parse`) |
| `display_name(NameKind::Item\|Block\|Entity, id)` | the translated, resource-pack-aware name: "Stone", "Cow" |
| `break_state()` | the block the local player is mining and how far along it is |
| `look()` | camera yaw and pitch |
| `region(a, b)` | up to 262 144 blocks as a palette + cells (`Region::at(x, y, z)`) |
| `raycast(origin, dir, {max_distance, include_liquids})`, `aim()` | block hit; what the crosshair targets (block or entity) |
| `find_blocks(center, {name, radius, max_results, exact})` | nearest matches first, radius up to 32 |
| `chunk_loaded(cx, cz)` | whether the chunk is in memory |
| `entities({radius, type, players_only, include_items})`, `entity(id)`, `players()` | nearby entities, one entity, the tab list. An `Entity` carries everything the game tracks: flags, health and max health, every attribute, entity properties, variant, owner, target, vehicle, held items, armor |
| `screen()` | GUI size and scale, fps, which screen is open, platform, server, language |
| `options_json()`, `option<Option::fov>()`, `option_text(key)` | settings |
| `perspective()`, `chat_history(n)`, `title_state()`, `resource_packs()`, `mods()`, `block_names()` | camera mode, recent chat lines, current title/subtitle/action bar, active packs, installed mods, every block name |
| `translate(key)` | localized string |
| `server_commands()`, `input_actions()`, `input_down(action)`, `mouse()` | server command names, action ids, held state of an action or `key.keyboard.x`, cursor in GUI pixels |

### `act::` (do things)

Free: `message`, `toast`, `title`, `play` (sound). Needs `control`: `say`, `run_command`, `select_slot`, `swing`, `use_item`, `use_on`, `attack`, `interact`, `break_block`, `look`, `look_at`, `jump`, `respawn`, `disconnect`, `drop`, `swap_slots`, `click_slot`, `open_inventory`, `close_inventory`, `drive(Inputs)`, `set_option`, `set_perspective`, `reload_resources`.

Free game actions: `open_screen`, `open_settings`, `screenshot`, `cycle_perspective`, `toggle_hud`, `copy_to_clipboard`. Needs `network`: `open_url`.

`drive` holds movement inputs for a number of ticks (`forward > 0` walks forward, `strafe > 0` goes right); call it every tick to keep moving. `look` takes the same yaw and pitch that `PlayerState` reports (yaw 0 faces south, pitch positive looks down). `break_block` breaks instantly in creative; in survival it starts mining, so keep the crosshair on the block and `drive` with `attack = true` until it breaks.

### `ui::` (HUD)

`ctx.hud("name", [](ui::Canvas& g) { ... }, z)` redraws every frame and only sends the layer to the game when the drawing changed. `Canvas` has `rect`, `gradient`, `border`, `line`, `text` (with `TextStyle{.color, .scale, .shadow, .align}`; `§` formatting codes work: `"§9§oMinecraft"` is italic blue), `item` (an item icon by id) and `image` (a resource-pack texture path). Coordinates are GUI pixels (the same units as the vanilla HUD); `g.width()` and `g.height()` are the current GUI size. `ui::text_width(text, scale)` measures with the game's font. Layers are hidden when the HUD is hidden or a menu is open.

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
| `SettingChanged` | handled for you by `ctx.on_change` / `ctx.on_press` | |

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
  waila/              the WAILA plugin: waila.cpp, waila.json, waila/ (api, layout, vanilla providers), tests/
  examples/c/         a plugin in plain C, no SDK
  tools/              perlin_settings.py (manifest -> typed handles), check_api.py (every operation is hosted and wrapped)
  tests/              meta_test, abi_test (wire format), sdk_test (fake host); build each with g++ -std=c++23 -Isdk
  prebuilt/           built .wasm files and their manifests
```

## Low-level range (`perlin/adv.hpp`)

For mods that need to reach into the game itself. Everything that changes the game needs the `control` permission, and every override carries a lifetime in ticks: it lapses on its own when the mod stops refreshing it, so a crashed or unloaded mod never leaves the player rolled, frozen or blind.

| Call | What it does |
| --- | --- |
| `adv::camera({.roll, .fovAdd, .fovSet, .offset*, .yawAdd, .pitchAdd, .ticks})` | Rolls the picture, changes the field of view, shifts the view, turns it without changing the aim. |
| `adv::camera_info()` | Eye position, angles, FOV, aspect and the column-major view-projection matrix. |
| `adv::to_screen(x, y, z)` | Projects a world point to GUI pixels (use it with `ui::Canvas` for ESP-style overlays, labels and markers). |
| `adv::set_velocity` / `add_velocity` / `motion` | Sets or adds to the local player's velocity (per axis). Elytra boosts, dashes, grapples. |
| `adv::teleport` | Moves the local player; the server sees ordinary movement. |
| `adv::model_pose({.pitch, .yaw, .roll})` | Rotates the local player's model about its middle after the glide lean. `yaw` is a spin around the body's axis, which in flight is a barrel roll. |
| `adv::lines` / `adv::boxes` | World-space lines and box outlines drawn through the world renderer. A schematic overlay (Litematica style) is one `boxes` call per frame. |
| `adv::find_block` / `adv::set_blocks` | Looks up a block by name and states, and changes blocks in the client's own copy of the world (ghost blocks, previews). |
| `adv::drop_packets` | Discards incoming packets by id for a while (needs `packets` too). |
| `adv::packet_counters` | How many packets of each id have arrived. |

`examples/barrel_roll` is a complete elytra barrel roll in thirty lines.

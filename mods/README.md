# Perlin plugins

Plugins are WebAssembly modules written in modern C++ (C++23). They run inside a sandbox in the client, can see almost everything the game knows, and can draw on the HUD, add commands and key binds, and react to game events.

Drop `name.wasm` (and optionally `name.json` and `name.png`) into `<data dir>/mods/`. Plugins hot-reload when the file changes (Options → Hot Reload On Save) and can be enabled, disabled and reloaded from **Pause → Mods**.

## Quick start

```cpp
#include <perlin/perlin.hpp>

using namespace perlin;

PERLIN_PLUGIN(ctx) {
    ctx.command("here", "/here", "prints your block position", [](const CommandCall& call) {
        if (auto p = game::player())
            call.reply("you are at {}", BlockPos(game::position(*p)));
    });

    ctx.on<event::ChatReceived>([](const event::ChatReceived::Payload& e) {
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
  "permissions": ["network", "packets", "control"]
}
```

`name.png` is the icon in the Mods screen.

## Permissions

Everything that only reads game state, draws UI, prints to your own chat or stores files in the plugin's own folder is always allowed. Things that act on your behalf or leave your machine have to be declared in the manifest and are shown in the Mods screen.

| Permission | Allows |
| --- | --- |
| *(always)* | player, inventory, world, blocks, entities, player list, options (read), screen and input state, translations, HUD drawing, local messages, toasts, titles, sounds, commands, key binds, storage |
| `control` | sending chat and commands, hotbar and inventory actions, swinging, using items, attacking, interacting, breaking blocks, looking, driving movement inputs, jumping, respawning, leaving the game, changing options |
| `network` | HTTPS requests |
| `packets` | sending raw protocol packets (login, handshake, resource pack and disconnect packets are refused) |
| `storage` | always granted; reads and writes under `<data dir>/mods/<name>.data/` only |

An operation without its permission returns `Error::denied()` instead of running.

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
| `options_json()`, `option(key)` | settings |
| `translate(key)` | localized string |
| `server_commands()`, `input_actions()`, `input_down(action)`, `mouse()` | server command names, action ids, held state of an action or `key.keyboard.x`, cursor in GUI pixels |

### `act::` (do things)

Free: `message`, `toast`, `title`, `play` (sound). Needs `control`: `say`, `run_command`, `select_slot`, `swing`, `use_item`, `use_on`, `attack`, `interact`, `break_block`, `look`, `look_at`, `jump`, `respawn`, `disconnect`, `drop`, `swap_slots`, `click_slot`, `open_inventory`, `close_inventory`, `drive(Inputs)`, `set_option`, `reload_resources`.

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

`ctx.after(delay, fn)` and `ctx.every(period, fn)` run on the tick clock and return an id for `ctx.cancel(id)`. `ctx.on_unload(fn)` runs before a reload.

### Commands and keys

`ctx.command("name", "/name <arg>", "description", fn)` adds a client-side slash command that appears in chat completion; the call receives `args`, `arg_or(i, fallback)` and `reply(fmt, ...)`. `ctx.bind("id", "Label", Key::G, fn)` registers a key that is active while no menu or chat is open. Players rebind plugin keys in **Options → Controls → Key Binds** (a section per plugin); the choice is stored in `<data dir>/mods/<name>.keys.json` (`{"id": "key.keyboard.g"}`).

## Writing against another game version

`game::meta()` returns the game version, platform, the plugin's permissions and the host API version (`abi::kApiVersion`). Operations a game version does not provide return `Error::status == UnknownOp`, so plugins written against newer headers degrade instead of crashing.

## Layout

```
mods/
  build.sh            build script (wasm64 with Emscripten; --wasm32 with wasi-sdk)
  sdk/perlin/         headers: perlin.hpp pulls in everything
    abi.hpp           wire format shared with the host
    rpc.hpp           Result/Error and the single host import
    game.hpp act.hpp ui.hpp io.hpp types.hpp runtime.hpp
  examples/           source for the prebuilt plugins
  prebuilt/           built .wasm files and their manifests
```

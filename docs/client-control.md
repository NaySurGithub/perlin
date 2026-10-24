# Local client control

Build Perlin, then start a background client with a loopback-only HTTP endpoint:

```sh
build/Perlin --hidden --control-port 19180 --offline --name PhysicsLab \
  --join 127.0.0.1:19142 --raknet
```

`--hidden` does not show the SDL window or capture the desktop mouse. The normal
client renderer, network session, input handling and player physics still run.
The interface is disabled unless `--control-port` is supplied. Browser origins
other than this localhost endpoint are rejected; mutation requests require JSON.

Set `PERLIN_DATA_DIR` to a separate data directory to isolate test settings,
accounts, logs and replay files from your usual client profile. Vanilla assets
must be available there (asset directories can be symlinked).

## Requests

```sh
# Connection, pose, velocity, simulation tick, last applied command, corrections.
curl http://127.0.0.1:19180/state

# Hold forward and sprint for exactly 40 local simulation ticks (two seconds).
curl -H 'Content-Type: application/json' http://127.0.0.1:19180/command \
  -d '{"op":"input","actions":["forward","sprint"],"ticks":40}'

# Look uses the same camera angles as the native client (degrees).
curl -H 'Content-Type: application/json' http://127.0.0.1:19180/command \
  -d '{"op":"look","yaw":90,"pitch":-20}'

# Stop all held actions. Omitting ticks holds input until replaced/released.
curl -H 'Content-Type: application/json' http://127.0.0.1:19180/command \
  -d '{"op":"release"}'

# Send a normal server command; server permissions remain authoritative.
curl -H 'Content-Type: application/json' http://127.0.0.1:19180/command \
  -d '{"op":"command","text":"/time set day"}'

# Request a rendered frame. The first request returns 202 while capture runs;
# repeat to receive JPEG bytes. Each request schedules an updated frame.
curl http://127.0.0.1:19180/frame -o frame.jpg

# The most recent 24,000 sent simulation ticks as newline-delimited JSON.
curl http://127.0.0.1:19180/trace -o movement.ndjson
```

`input` accepts the action IDs from `KeyMapping.cpp`, with or without `key.`.
This includes movement, jump, sneak, sprint, use, attack, inventory and hotbar
actions. Requests also support `chat` (`text`), `key` (`key`, `down`), `mouse`
(`x`, `y`, `button`, `down`) and `shutdown`.

Commands return `202` and a monotonically increasing `id`. They are applied on
the game thread; wait until `/state.applied` reaches that ID. `commandError`
reports a failed application. Bad requests return `400`, wrong content types
`415`, and a full pending queue `429`. Raw key and mouse requests are useful for
menus; gameplay movement should use `input` so durations follow simulation ticks.

For server commands, `commandReplies`, `lastCommandSuccess` and
`lastCommandOutput` report actual `CommandOutputPacket` replies separately from
the local send acknowledgement. A locally accepted request does not prove the
server executed it. The player's server-visible name can acquire a suffix when
another player already uses the requested name; check command replies rather
than assuming a name uniquely identifies the controlled actor.

`{"op":"probe","x":0,"y":100,"z":0}` selects a block to inspect. State then
includes `probeLoaded`, `probeName` and local collision boxes in `probeCollision`.
Check `probeLoaded` before interpreting the shape: unloaded terrain is blocked
by the normal collision adapter.

## Physics validation

`corrections` counts actual BDS player prediction correction packets; it is not
a synthetic pass counter. Capture a baseline after spawning or teleporting and
compare it after each movement scenario and a settling interval. A result is
valid only if the client stayed connected and initialized and actually sent the
requested movement. The trace records the resulting positions, velocities,
inputs and collision flags for each sent tick.

Zero corrections on a test route does not establish complete native BDS parity.
Keep the server's strict movement checks enabled and record which terrain,
attributes, movement modes and multi-tick transitions were exercised. Do not
hide corrections, disable authoritative movement or increase tolerances to make
a test pass.

The HTTP/input self-test covers action expiry and repeat presses, server input
locks, blocked input, command validation and origin checks, returned state,
trace delivery, and JPEG colour conversion. Live checks also exercise GPU frame
readback from the hidden Metal renderer and shutdown with a pending capture.

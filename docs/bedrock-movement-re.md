# Bedrock movement & placement — live RE notes

Source of truth: IDA 9.4 (Windows, under Wine) with the China client database, driven through
`ida-pro-mcp` on `http://127.0.0.1:13337/mcp`. Helper: `~/.local/share/ida/idamcp.py`.

```bash
# one query
python3 ~/.local/share/ida/idamcp.py decompile '{"addr":"0x1411d7480"}'
python3 ~/.local/share/ida/idamcp.py list_funcs '{"queries":[{"filter":"*travel@Mob@@*","count":20}]}'
```

Database identity (from `server_health`):

| field | value |
|---|---|
| idb | `~/.local/share/ida/db/china-1.16.201.i64` |
| module | `Windows Client 1.19` |
| imagebase | `0x140000000` |
| Hex-Rays | ready |

The DB's own assert paths say `h:\code\rtx_1.23\handheld\src-client\...`, so it is a 1.19/1.23-era
build. Algorithms and constant families carry forward; anything version-sensitive must be diffed
against server behaviour, not assumed.

## Movement — addresses

| function | address | size |
|---|---|---|
| `LocalPlayer::_aiStep(IPlayerMovementProxy&)` | `0x1411d5730` | 0x24c |
| `LocalPlayer::aiStep()` | `0x1411d5380` | 0x3a5 |
| `Player::_aiStep(IPlayerMovementProxy&)` | `0x141ed65f0` | 500 lines |
| `Mob::_aiStep(IMobMovementProxy&)` | `0x141ea1fb0` | — |
| `LocalPlayer::_travel(proxy, xa, ya, za)` | `0x1411d7480` | 0x2a5 |
| `LocalPlayer::travel(xa, ya, za)` | `0x1411d7350` | 0x127 |
| `Mob::travel` | `0x141e9dde0` | 0x127 (proxy forward) |
| `PlayerMovement::updatePlayerOnInput` | `0x14199d860` | 0x61f |
| `PlayerMovement::updateSwimTravel` | `0x14199de80` | 0x350 |
| `PlayerMovement::updatePlayerRiding` | `0x14199e1d0` | 0x402 |
| `PlayerMovement::isFlying` | `0x14199d820` | 0x3e |
| `LocalPlayer::jumpFromGround(proxy)` | `0x1411de9a0` | 0xa0 |
| `LocalPlayer::jumpFromGround()` | `0x1411de870` | 0x128 |
| `Mob::jumpFromGround()` | `0x141ea2660` | 0xf2 |
| `LocalPlayer::applyFinalFriction(f, bool)` | `0x1411d7730` | 0x10c |
| `Mob::applyFinalFriction(f, bool)` | `0x141e9f590` | 0x60 |
| `Actor::_move(proxy, delta)` | `0x141c55310` | 0x1926 |

## Verified behaviour (read out of the binary, not assumed)

1. **`Mob::applyFinalFriction`** (`0x141e9f590`) snaps a horizontal component to exactly zero when
   `|delta| <= 1.1920929e-7` (FLT_EPSILON) before applying friction. A naive `vel *= friction`
   never fully stops, and that residual is visible to server-side prediction.
2. **`LocalPlayer::applyFinalFriction`** (`0x1411d7730`): outside VR it forwards `finalFriction`
   unchanged; in VR with movement it scales by `min(pow(xa*xa + za*za, 0.2), 1)` unless
   `disallowVRDecel` or actor status flag 55 is set. So non-VR and VR do **not** share friction.
3. **`LocalPlayer::_travel`** (`0x1411d7480`): calls `PlayerMovement::updateSwimTravel` on the
   position delta every tick, and in VR-water mode lerps the water AABB by `0.55` and zeroes
   `mPosDelta.y` unless the AABB contains material 5.
4. **`LocalPlayer::_aiStep`** (`0x1411d5730`) packet order is: `sendPlayerInput()` → `canMove` →
   `setWasAutoJumping` → `Mob::_aiStep` → `Player::_aiStep` → (if not sleeping) either
   `sendInput()` (server-authoritative) or `sendClientAuthInput()`. Perlin must emit its movement
   packet at the same point in the tick, or the server sees a one-frame-stale input.
5. **`PlayerMovement::updatePlayerOnInput`** (`0x14199d860`): input is scaled by `0.35` on both
   axes; deadzone `max(|x|,|y|) < 0.0099999998` zeroes it; vertical adjustments use
   `+0.15`, `+0.05`, `-0.22`, `-0.15`, `+0.1`, `-0.04` depending on state.

## Next pulls

1. `PlayerMovement::updatePlayerOnInput` in full — sprint/sneak/fly input derivation.
2. `Mob::_travel` real body (the proxy at `0x141e9dde0` only forwards) — gravity, ground friction,
   air control.
3. `Actor::_move` (`0x141c55310`) — collision resolution order (axis order matters for bridging).
4. `LocalPlayer::jumpFromGround` — jump impulse and jump-trigger conditions.
5. Placement: search `*placeBlock*`, `*BlockPlacement*`, `*HitResult*` — reach, raycast, overlap.

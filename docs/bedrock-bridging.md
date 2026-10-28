# Bedrock bridging

With **Bedrock Bridging** enabled in gameplay settings, holding **Use Item / Place Block** with a block in hand
follows Bedrock Edition's held-placement rules, so every bridging technique (speed, god, breezily, telly, jump,
sneak bridging) behaves as it does in the game. With the option off, Perlin uses ordinary ray targeting and a fixed
repeat.

The rules live in `src/client/player/BuildIntention.{hpp,cpp}`, ported from
[Cinnabar](https://github.com/cinnabar-mc/cinnabar) under the PolyForm Noncommercial License 1.0.0
(`third_party/cinnabar/LICENSE`).

## Target

- The first click of a press uses the crosshair's block face, as normal.
- While use stays held, after a successful placement, the next attempt clicks the **last placed block** on the face
  pointing along the player's **dominant movement axis** (sneaking keeps the crosshair's own support). Walking
  backwards off an edge therefore keeps extending the bridge without aiming at a face.
- Two adjacent successful repeats without sneaking establish a **build line**. From then on the next cell on that
  line is clicked whenever the look ray, from the eye to the 5.7 block reach, passes through it, wherever the
  crosshair is pointing. A failed attempt does not advance the line. Releasing use, emptying the hand or leaving
  gameplay clears the line.

## Repeat timing

| State | Interval |
| --- | --- |
| Sneaking, using a block, or placing before a line exists | 300 ms |
| Standing still | 200 ms |
| Moving | `min(900 / speed, 180)` ms, speed in blocks per second |
| Survival / adventure floor | at least 100 ms |

A fresh press always acts immediately.

## Prediction

Bridge placements are predicted against the player's actual body, without the extra airborne padding used for
ordinary placement, so jump bridging shows blocks in time. The server stays authoritative; rejected placements are
corrected by the existing prediction expiry.

## Validation

`build_intention_selftest` covers the repeat table, the dominant-axis face, line establishment, continuation off
the crosshair, failed attempts, and sneaking.

```sh
cmake --build build --target build_intention_selftest && ./build/build_intention_selftest
```

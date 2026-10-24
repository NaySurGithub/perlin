# Bedrock bridging

With **Bedrock Bridging** enabled in gameplay settings, hold a block, look
forward and down into the empty space beside your footing, and press or hold
**Use Item / Place Block**. The outline highlights the existing support block
you are placing against, using its normal selection shape. Walk,
sprint, crouch or jump along the bridge while holding use to keep extending it.
The same Action::Use path serves mouse, controller and touch bindings.

The fallback works in all four horizontal directions. Diagonal paths use
face-connected steps; touching a block at a corner alone cannot anchor a new
floating diagonal block. Real block and entity ray hits retain priority.
Disabling the option restores ordinary ray targeting and placement repetition.

## Target selection

`Bridging.cpp` projects the look direction onto the top of a physical support
under the player's footprint, including the remaining footprint after the
player's centre crosses an edge. It searches the actual collision boxes, so
slabs, stair steps and narrow supports use their real heights and footprints.
Supports may be up to 1.5 blocks below the feet during a jump; jumping does not
require an onGround flag. A falling player below the support is ineligible.

A destination must be a loaded, replaceable cell sharing a horizontal face
with the support. The solver checks reach, finite coordinates and player-body
overlap. Liquids, plants, missing chunks and unknown block names cannot supply
an anchor. Interactive supports are eligible while sneaking; air use otherwise
does not unexpectedly open them. The outgoing transaction clicks the support's
actual side face, with a block-relative point, and places into the neighbouring
destination. The attack and pick-block targets are never replaced.

The previous helper looked one block below the supporting block at integer
foot heights, only checked the player's centre cell, and chose a neighbour by
an arbitrary distance from the ray. It also treated most non-air names as solid.
That helper is removed.

## Holding use, prediction and feedback

- A fresh use press acts immediately. Holding use can move to a new placement
  destination once per 50 ms game tick. A stable destination retries after
  200 ms. Failed send attempts are also throttled. Releasing use, emptying the
  hand, entering spectator mode or leaving captured gameplay resets repetition.
- The target is recomputed for each frame. Continued placement swings the hand,
  sends the usual use transaction and produces placement sound. Failed sends do
  not produce a predicted block or placement sound.
- Valid bridge placements use the actual held shape/body test for prediction.
  They do not inherit the old extra 0.6-block airborne padding that prevented
  timely jump bridging. Ordinary placement keeps its previous prediction policy.
- Slab side clicks select upper/lower geometry using the transmitted float click
  height. The overlap check uses that placement geometry; the outline uses the
  existing support's selection geometry, including its slab half. Vanilla slab
  prediction resolves `top_slot_bit` or `vertical_half`, preserves other block
  states, and verifies the resulting runtime-ID mapping. Unknown permutations
  wait for the server. Other stateful blocks retain the existing prediction
  exclusions.
- Sending validates the face, current held block/count, loaded destination and
  replacement state. A stale bridge plan is discarded if reclassification no
  longer gives the intended destination. Prediction also rechecks replacement
  state, so it does not overwrite a newly occupied or unknown cell.
- Existing server confirmation and prediction expiry remain authoritative.
  The stop-use action reports the most recently sent destination rather than
  the first block of a long held-use sequence. Menus and release end active use.

## Validation

`bridging_selftest` exercises fixed scenes and complete held-use routes. It checks
expected anchors, destinations, Bedrock faces and click coordinates; negative
coordinates; crouching; edge overhangs; jump height limits; diagonal steps;
lower/upper slabs and narrow supports; interactive, replaceable, occupied and
unloaded cells; malformed inputs; reach; and repetition/release timing.

Six 160-tick routes cover full blocks and both slab halves, walking and jumping,
fixed look pitch, continuous use, prediction becoming the next support, and
multiple chunk borders. All **2,217 checks pass** on optimized ARM64 and
x86-64/Rosetta builds and under AddressSanitizer/UndefinedBehaviorSanitizer.
The full local application builds and **61/61 CTest targets pass**.

```sh
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

Live BDS gameplay was not exercised in this run. The tests verify the client
selection, geometry and input decisions; they are not native BDS conformance
fixtures. Block-specific permission, placement and permutation rules are still
decided by the server.

For a live playtest, start at the edge of a platform with full blocks, hold use
while walking and sprint-jumping forward, then repeat along lower and upper slab
rows. Check all four directions and chunk borders. Release use or open a menu
and verify placement stops. Toggle Bedrock Bridging off and verify that empty-air
targets no longer receive an outline or reacharound placement. Check that a
server-denied placement is corrected through the existing prediction expiry.

Outline screenshots cover full blocks and both slab halves. These are
offline renderer captures, not multiplayer placement tests.

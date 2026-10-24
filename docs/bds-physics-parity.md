# BDS physics parity audit

This is an ongoing port. Full player physics parity is **not established**.
The target remains the entire BDS simulation and the missing Perlin systems
needed to reproduce it, including integration and multi-tick behavior. The
numeric tests below are a small part of that requirement.

## Authoritative reference

The numeric evidence ledger below uses the IDA MCP instance containing
**Windows Server 1.26.10**, image base `0x140000000`.
IDA's stored original-input SHA-256 is
`f35469677a9d0db236e8559e9e72c4d6532230edf54497e88677eceff4cde539`.
The database is mostly stripped. System-name string references, registration
factories, vtables, ECS iteration wrappers, and callbacks establish provenance.
Decompiler argument names and inferred types are not treated as semantic names.

## Live server checks, 2026-10-24

The hidden native client also runs against **Windows BDS 1.26.52.3** under
Wine 11 over localhost RakNet. Movement strictness remains enabled and the
server's default correction threshold remains unchanged. A game-time query
advanced 77 server ticks over approximately four seconds, checking that Wine's
clock was progressing normally before interpreting movement results.

The movement attribute packet during sprint contains current speed `0.13` and
modifier `d208fc00-42aa-4aad-9276-d5446530de43`, named `Sprinting speed boost`,
amount `0.3`, operation/operand `2/2`. Perlin previously multiplied this current
value by another 1.3 in ground travel. The session now replaces only that known
modifier with the locally predicted sprint state, and travel consumes the
resolved attribute once. This also preserves other received movement modifiers
and supplies the resolved value to water acceleration and FOV prediction.

A separate **Minecraft 26.52 native client** IDA database supplies supporting
producer evidence; these addresses are not Windows BDS addresses:

- Sprint action body `0x10678e628` calls toggle `0x106783b50`, which changes
  actor sprint flag bit 3 and queues an attribute add/remove request.
- Modifier initialization `0x10aca67fc` identifies the UUID above and float 0.3.
- Attribute registry `0x105f49030` maps MovementAttributes slot 0 to
  `minecraft:movement`; updater `0x107034a50` copies the current attribute.
- PlayerResetMovementSpeed traversal `0x10612e098` copies that optional value
  into MovementSpeed, or zero when absent, without an extra sprint multiplier.
- Player air-speed traversal `0x1070f1f14` separately selects float 0.02 or
  float 0.026 from the actor sprint flag.

The same live 40-tick sprint changed from **eight movement corrections to zero**.
The subsequent matrix produced zero movement corrections in all 12 cases:
forward/back/left/right walking, diagonal walking, sprinting, sneaking, standing
jump, walking jump, sprint-jump, sneak-jump, and rotated diagonal walking.
Four raw corrections occurred during teleport setup and remain counted and
reported separately. This is bounded runtime evidence, not overall BDS parity
or proof that all network correction behavior is correct. All 62 CTest tests
pass after supplying resolved attributes in the legacy trace adapter and the
sprint-distance fixture; expected distances and assertion tolerances are unchanged.

Five additional cases with Speed I, Slowness I, and combined Speed II/Slowness I
also produced zero movement corrections, including walking and sprinting. This
checks that removing the known sprint contribution does not discard the other
received modifiers. A fresh platform at y=160–162 changed from loaded air to
stone with collision at all 11 probes spanning positive/negative chunk borders.

The localhost browser view has also been exercised against the actual hidden
renderer: a quick E tap opens inventory, a pointer click opens the recipe panel,
native text input receives an unsent draft, Escape restores logical capture,
and the command form receives a real server game-time reply. These interface
checks do not substitute for broader physics coverage.

A subsequent still-water matrix with Water Breathing maintained health 20 and
air 300 throughout. Idle, forward, backward, sideways and ordinary water-jump
cases produced zero movement corrections. Sprint swimming produced one and
swim-plus-jump produced eleven. These remain unresolved; native swim input,
vertical control, surface behavior and transition ordering are under audit.
An earlier water run ended in drowning and is not used as a healthy swimming
comparison. The control endpoint now exposes health, air and death state so
scenario runners can detect that interruption explicitly.

The terrain handler also accepts first block updates in an unallocated section
of an existing loaded chunk, allowing Level to allocate the section. Previously
the section-presence guard silently discarded those updates, leaving holes in
the client's collision terrain. Unloaded chunks are still rejected.

See [local client control](client-control.md) for the hidden launch, live browser
view, tick-bounded input, block probes and raw correction telemetry. Broader
terrain, fluid, effect, mode, entity and authority-transition coverage remains
required below.

## Evidence and implementation ledger

| Behavior | BDS evidence | Port and verification status |
| --- | --- | --- |
| Fluid state and body sensing | Registration `0x1402D15D0`; `LiquidBlocksFetch` callback `0x140FC98E0`; common collector `0x140FC94C0` → `0x140FC8FA0`; bounds conversion `0x1447976A0`; InWater/InLava callbacks `0x140E0E820` / `0x140E0E6F0` | 4,256 new native executions cover state transitions, contracted bounds, material priority, cell traversal and boundary records. Ordinary Step uses the ported state callbacks, float contraction and single-body collector. Flow producers, ready/unready behavior, secondary bodies, head sensing, passenger integration and request consumption remain incomplete. See the fluid section below. |
| Travel-intent attenuation | `MobTravelIntentSystem` string `0x1468B56F8` → factory `0x140EB2E40` → vtable `0x1466D1D80` slot 5 → wrapper `0x140EBF3A0` → loop `0x140EB2C80` → callback `0x140EB2C20` | Three float intent lanes multiply by `0.98f`; turn scalar multiplies by `0.9f`; attenuated vector is copied. Numeric helper verified against 109 executed BDS cases. Step uses its horizontal intent output. The meaning/integration of the other lanes still needs tracing. |
| Friction arithmetic | Ground/air/flying registration `0x14103DBC0`; run wrappers `0x141051710`, `0x1410512D0`, `0x141050E90`; common callback `0x14103D9D0`, shared tail `0x14103D8F0` | 3,052 BDS executions cover both membership branches, vertical flag, coefficient products, threshold boundaries, and signed zero. Step uses the corrected per-component kernel. Metadata builder `0x14106DCC0` identifies the special membership as `VexFlagComponent` (hash `0x02BE52F4`), coefficient/vertical fields as `MobTravelComponent` offsets 28/40, and the multiplier as `FrictionModifierComponent`. The full ECS selection logic and coefficient writers have not been ported. |
| Jump-delay decrement | `DecrementNoJumpDelaySystem` registration `0x1402CBA10` → vtable `0x1466D1E60` slot 5 → wrapper `0x140EC0610` → loop `0x140EB15B0` → callback `0x140EB0DA0` | Eight machine-code cases including negative values and integer boundaries. Step calls the verified decrement helper; system order remains under audit. |
| Sine-table construction | Static initializer `0x1401162A0` → builder `0x1454F9850` → bundled vector sinf dispatcher `0x146251E10` → SSE2 `0x146251EA0` / SSE4 `0x1462527E0` | Both machine-code implementations produce the same 65,536 entries for the builder's float arguments. Portable construction matches every entry bit-for-bit. Replaces Perlin's double-argument host-libm table. This proves the bounded table, not equivalence of general-purpose sinf. |
| Sprint-jump impulse | Ground jump routine `0x140EAFAF0`, instruction range `[0x140EAFD9C, 0x140EAFDFA)` | Direct degrees-to-index conversion and two horizontal impulse operations ported. 277 machine-code block executions record both lookup indices and resulting velocities. Full jump eligibility, attribute/effect sources, special surfaces, and jump-system ordering remain under audit. |
| Ground jump callbacks | `0x140EAFAF0`; alternate path `0x140EAFFD0` | 1,536 complete callback executions match checked outputs bit-for-bit on ARM64 and x86-64. Ordinary Step uses optional height presence, native effect arithmetic, raw block properties and previous-jump position. Request production/selection, real block-property production and trigger consumption remain incomplete. |
| Water travel acceleration | `WaterTravelSystem` registration `0x1402CE3E0` → vtable `0x1466D2A60` → wrapper `0x140F27220` → loop `0x140F1E920` → callback `0x140F1E690` | Complete callback executed for 6,912 cases covering optional movement/underwater attributes, player membership and registry generation, dolphin/ground tags, enchantment clamp and swim multiplier. Step uses the verified arithmetic and own-player underwater attribute. Full attribute modifier production and scheduler behavior remain under audit. |
| Ground acceleration arithmetic | `GroundTravelTypeSystem` factory `0x140E0B080`; vtables `0x1466D0238` / `0x1466D0270`; wrappers `0x140E25FA0` / `0x140E26110`; callbacks `0x140E172E0` / `0x140E16EE0`; kernels `0x140E105A0` / `0x140E104A0` | 336 executions of both complete kernels with a synthetic block-source query. Covers ordinary/special/air identities, ability flag, zero friction fallback and differing runtime/definition friction. Step uses the verified ratio-cubed-then-speed evaluation order. Definition-friction selection, feature gates and exemption component identity still need integration. |
| Swept contact and overlap recovery | `ActorMoveSystem` factory `0x140BB1410` → vtable `0x1466C4F60` → wrapper `0x140BC7D40` → loop `0x140BBD5D0` → callback `0x140BBD410` → sequential kernel `0x14103BE00` → contact kernel `0x144795D10` | Contact/movement kernels ported and integrated in Step. 2,404 contact cases and 512 complete sequential-movement executions compare all output fields exactly, including penetration depth, both velocity candidates, axis, total displacement and final bounds. Covers non-positive stationary dimensions, `1e-6` contact tolerance, ties, reverse shape order, cross-axis recovery, and recovery limits. Broad-phase shape selection/order is not yet equivalent. |
| Recovery configuration/state | `ConfigureDepenetration` → factory `0x140BB1200` → vtable `0x1466C4F98` → callback `0x140BBD770`; update factory `0x140BB1620` → vtable `0x1466C4F28` → callback `0x140BBD140` | Added persistent state, per-axis recovery limits, and flag transitions, included in prediction snapshots. 384 configuration and 128 update executions agree exactly. `PUSH_TOWARDS_CLOSEST_SPACE` is BDS actor flag 109 (word 1 bit 45), established from the enum string-view table at `0x146AFBD80`; wired from own-player server metadata into recorded input. Sources of recovery history, custom minima and non-default initial flags still need auditing. |
| Auto-step callback and eligibility | `AutoStepSystem` factory `0x140C220E0` → vtable `0x1466C64B8` → wrapper `0x140C456E0` → loop `0x140C31A20` → callback `0x140C319E0` → `0x140C1C8E0`; filter `0x140C1C5D0` through factory `0x140C21ED0`, vtable `0x1466C6448`, wrapper `0x140C45900` | Complete callback executed for 512 cases with supplied shapes, including ceilings, altered body heights, shape order and nonzero positions. Filter executed for 256 combinations, including upward/downward collisions, flying, ground and one-shot flags. Ported helpers are integrated into Step. One-shot requests are consumed even when ineligible. Secondary actor shapes are empty in this oracle; their movement and producer systems remain unverified. |
| Broad-phase region and displacement cap | `MoveCollisionSystem` callback `0x140F90EA0` and cache helper `0x140F90C90` | Complete callback executed against an empty synthetic world for 512 cases. All six region bounds and three capped displacement lanes agree exactly, including thin/zero/inverted bounds, large coordinates, signed zero and movement above length 16. Integrated into Step; StateVector velocity is retained separately from the capped displacement. Cache append/subtraction/refresh are now separately verified below. Complete block/entity collection and request lifecycle remain unverified. |
| Final movement position/flags/velocity | `FinalizeMoveSystem` vtable `0x1466C71F0` → wrapper `0x140C744C0` → loop `0x140C64040` → callback `0x140C638A0`; binding `0x140C69800` | Complete callback and native tag-decision helpers executed for 1,792 cases. All position/velocity lanes and five tag memberships agree bit-for-bit. Includes absent/present abilities, no-clip flag preservation, zero-motion ground retention, exact/epsilon collision distinctions, MoveRequest byte 136, and clearing nearby-mob membership. Integrated in ordinary/flying movement; no-clip retains flags. Perlin uses feet coordinates (offset zero); native OffsetsComponent production, complete world-query integration, other collision responses and full ECS selection/order remain under audit. |
| Vertical collision and restitution | `VerticalCollisionSystem` factory `0x1402CCE40` → vtable `0x1466D2BE8` → wrapper `0x140F1D4A0` → callback `0x140F1D820`; block slot 65; `ApplyRestitutionSystem` factory `0x140C1BBE0` → vtable `0x1466C6710` → callback `0x140C1BA90` | 1,856 complete callback chains with ordinary/bed/slime block handlers, native ECS visitors, restitution creation/update, minecart damping and optional player bounce writes. Position, velocity, impulse, component membership and event record fields agree exactly. Step uses the verified velocity response without the old speed cutoff, including flying, and applies/consumes pending restitution before drag. Native cache collection/lifecycle, BounceComponent production/event consumers, other block overrides and scheduler predicates remain unported. |
| Supporting-block selection | Selector `0x14478D6B0`, called by vertical collision `0x140F1D820` on MoveRequest cache offset 88 | Complete selector executed without runtime hooks for 1,280 cases; returned block identity, floored coordinates and adjusted query bounds match exactly on ARM64 and x86-64. Integrated into Step using the collected shapes and parallel material metadata; replaces the feet-cell probe. Cache population and refresh/subtraction are separately verified below; actor shapes, complete world-query predicates and request lifecycle remain under audit. |
| Unavailable-chunk collision walls | BlockSource query `0x143E94280` → `0x143E9FFF0`, optional prepass `0x143EA1990` | Complete prepass executed for 564 cases with synthetic chunk lookup. Every visited chunk coordinate, its order, all barrier bounds and preservation flags match exactly on ARM64 and x86-64. Step collects these walls before terrain and skips missing terrain columns. LevelCollision supplies resident/missing state; other native chunk states and complete query selection/lifecycle remain under audit. |
| Terrain query traversal and below-world boundary | BlockSource query `0x143E9FFF0`; height accessors `0x143E8F460` / `0x143E8F450` | 382 complete native query executions with synthetic column/palette access, empty block geometry, chunk-wall prepass disabled and the native world-border skip predicate enabled. Exact agreement on every acquired column, cell visit and below-world boundary, including signed 16-bit height extremes. Step now uses float-expanded query ranges, height clipping, native visit order and the below-world slab; LevelCollision supplies stored chunk limits. Block geometry dispatch, dimension-specific limit production, full adapter/query predicates and world-border handling remain under audit. |
| Common block collision collector | BlockLegacy slot 7 (`0x1467BA4A0 + 56`) → `0x143F386B0` | 310 complete collector executions with supplied offset-accessor/fallback-AABB services cover absent, disabled and enabled components; optional queries; invalid and touching boxes; and arrays through 32 shapes. Return flags and all output bounds agree exactly on ARM64 and x86-64. Step shares its float translation/strict filtering. Removed the eight-box BlockShape cap and voxel-box gap-filling merge. Actual component/offset producers, block overrides and geometry asset provenance remain under audit. |
| Position-seeded block offsets | Seed constructor `0x14048C340`; generator `0x1441FC5A0`; description producer `0x1441FC8B0`; schema registration `0x1441FC9D0` | 137 native 3D seed states, 1,233 complete generator executions and 18 description-production cases agree bit-for-bit on ARM64 and x86-64. Step accepts resolved offset components from World shapes and shares the verified generator/collector path. Description range scaling is ported; canonical custom definition parsing is connected; full loader constraints/default semantics and vanilla component lookup remain under audit. |
| Ground/air gravity and vertical drag | Gravity registration `0x14103EC40`; gravity callback `0x14103E8F0`; apply `0x14104F970`; drag `0x141051E00` | Ported gravity/scaffolding flags, slow-falling selection and fall-distance reset. 288 standalone selection cases plus chained callback cases verify exact results. Own-player flag metadata is recorded with input; missing initial metadata defaults to gravity enabled and connection changes reset validity. Fall-distance accumulation and other gravity variants remain unported. |
| Levitation, player water gravity and flying vertical drag | Levitation callback `0x14104F680`; water callback `0x14104FC50`; flying callback `0x141051C40` | Float evaluation order ported; land drag follows levitation. Swimming suppresses player water gravity independently of HAS_GRAVITY. Flying drag resets fall distance. 3,456 chained native callback cases cover travel modes, gravity, scaffolding descent, auto-climb, discard friction, effects and signed zero. Full scheduler execution is not claimed. |
| Water drag and depth-strider retention | Callback `0x14103D110`, query `0x1410640F0`; depth-strider registry initializer `0x143C02F40`, constructor `0x143C0D370`, vtable `0x146795F58` slot 5, max-level callback `0x143972690` | 4,320 complete callback executions agree exactly, covering grounded/airborne, sprinting, custom drag, swim multiplier, enchantment clamp and signed zero. Step uses the helper with recorded armor enchantment levels. Non-default WaterMovement producers remain under audit. |
| Movement effects and dolphin swim multiplier | Apply helper `0x14387A590`; `MovementEffectsTick` callback `0x1410114B0`; `DolphinBoostSystem::swimSpeedModifier` callback `0x140B6FD10` | 2,232 native executions verify longer/infinite refresh rules, signed wrap, expiry/reset and swim selection. Own-player MovementEffectPacket events now enter recorded input; state survives prediction replay and ticks after movement, including flight/no-clip. Nearby-dolphin prediction, effect-source tick reconciliation and remote entities remain unverified. |
| Rotation direction and gliding velocity | Direction helper `0x143261390`; GlideMoveSystem registration `0x1402CE3E0` → factory `0x140C6BB90` → vtable `0x1466C7110` → run `0x140C7EC30` → loop `0x140C72F30` → wrapper `0x140C72EE0` → callback `0x140C66F70` | Replaces the old double/libm glide formula with the BDS float callback, including slow falling, glide boosts, drag and fall-distance handling. 836 direction cases, 704 gliding cases and 10 large/non-finite table-index cases match exactly. Three native callback sequences also match Step velocity/effect state across 288 ticks. Native rotation-history production, gliding eligibility/transitions, collision damage, positions and complete tick scheduling remain unverified. |
| Relative movement and bundled trig | DefaultMoveSystems registration `0x140B52880`; Ground/Air vtables `0x1466C47A8` / `0x1466C47E0`; callback `0x140B52740` → helper `0x140B52390` → kernel `0x1432674C0`; sincos wrapper `0x146251E30`, dispatch `0x146251E10`, vector sine `0x146251EA0` / `0x1462527E0`, rare helper `0x146253D80` | Full three-lane arithmetic and bundled trig ported, including large finite range reduction, tiny arguments and exceptional-value output bits. 7,248 trig and 3,584 relative-movement cases match, with both CPU dispatch variants. Independently cross-checked by executing the original BDS routines as native x86-64 code under Rosetta; this caught an emulator NaN-propagation discrepancy. Integrated into Step in place of host sin/cos. Full input/travel selection and other floating-point kernels remain under audit. |
| Collision-cache append, subtraction and refresh | Converter `0x140F90C90`, difference helper `0x140F924E0`, complete callback `0x140F90EA0` | 512 append, 569 subtraction, 512 empty-world refresh and 256 nonempty refresh executions, plus native constructor defaults, agree bit-for-bit on ARM64 and x86-64/Rosetta. Covers append retention, null block metadata, incoming preservation flags, split/merge arithmetic and ordered incremental queries. Step uses the cache model within one request; native cross-phase request lifetime and nonempty full-world callback interactions remain unverified. |
| Block slowdown application, accumulation and player immunity | Apply vtable `0x1466C66A0` → wrapper `0x140C2FD00` → loop `0x140C1EC20` → callback `0x140C1EF20`; accumulator `0x140B58EA0`, applied-record constructor `0x140B80D80`, fall-distance callback `0x140C1EF10`, player callback `0x140C1EE80` | 1,024 apply, 768 accumulation, 256 fall-distance and 256 player-immunity executions agree exactly. Apply is additionally checked by original native x86-64 execution. Step consumes stored multipliers before collision, clears velocity there, retains later restitution, and uses verified web accumulation/fall reset after all movement modes. EntityInside traversal, other block producers, mob exemptions and full scheduler integration remain under audit. |
| Sneak eligibility and cached-shape edge clipping | Factory `0x140E7B1D0` → vtable `0x1466D1058` → run `0x140E8DED0` → loop `0x140E817E0` → wrapper `0x140E81520` → kernel `0x140E76C70`; filter `0x140E76AD0` | 1,024 complete executions with synthetic sparse-pool state and supplied cached shapes, without runtime hooks, match bit-for-bit on ARM64 and x86-64. Step clips after cache collection with the verified max-step/float arithmetic and only clears near-zero velocity lanes. The optional view is MoveInputComponent and actor flag bit 6 is TEMPTED; full input producers and ECS/scheduler integration remain under audit. |
| Entity-inside cell traversal and region readiness | EntityInside callback `0x140B58960` → query `0x1410EE9B0`; LocalConstBlockSource vtable `0x1467AF088`, slot 14 → `0x143E8D580` | 804 traversal and 512 readiness executions agree bit-for-bit on ARM64 and x86-64. Step uses native cell-range rounding, complete visit order and chunk readiness before contact accumulation. Non-default component bounds/expansion producers, block-specific predicates and full EntityInside ECS lifetime remain under audit. |

## Required remaining coverage

This list is a scope floor, not a closed list of everything BDS contains. Add
subsystems discovered in the binary rather than excluding them from the target.

- Tick scheduling, ECS filters, component state, input normalization, control
  suppression, sprint/sneak/item-use/pose transitions, and saved prediction state.
- Ground and air acceleration; gravity and drag; jump variants; attributes,
  abilities, effects and enchantments; climbing and wall/ledge behavior.
- Water/lava sensing, submersion, flow, swimming, fluid acceleration, fluid exit,
  buoyancy, bubbles, and transitions between movement modes.
- Creative/ability flight, no-clip, gliding and all mode transitions.
- Broad phase and narrow phase collision, overlap recovery, axis ordering,
  stepping, support selection, friction surfaces, block movement multipliers,
  crawling, partial shapes, world borders, and unavailable chunks.
- Slime, beds, honey, webs, berry bushes, powder snow, scaffolding, soul-speed
  surfaces, pistons/moving blocks, and other block-specific physics.
- Entity interaction, pushing, knockback, impulses, mounts, boats, minecarts,
  passengers and other actor/vehicle physics needed by a vanilla client.
- Network authority, correction and replay; dimensions/teleports; server-driven
  motion; bitwise arithmetic where observable; platform/compiler behavior.
- Independent BDS-derived single-tick and multi-tick scenarios for every path,
  transitions and interactions; real client/server checks.
- Final commit/push to the fork branch and a PR targeting upstream `main`, with
  a detailed description, remaining limitations, reproducible evidence and an
  explicitly scoped accuracy percentage.

## Reproducing the numeric evidence

Use the selected BDS IDA context to execute `tools/bds_reference/export_ida.py`,
then call `export_reference()`. Save its JSON return to
`build/bds-reference.json`. For MCP output limits, assign the return to a local
variable and read it in chunks of at most 24,000 characters before joining it.
The exporter only reads the database. The JSON includes executable bytes and
must stay outside the committed source tree.

On a local IDA host sharing the workspace, load the exporter into a fresh
dictionary (`ns = {}; exec(Path(script).read_text(), ns)`) and save
`ns['export_reference']()` directly with `Path(...).write_text(...)`. The fresh
namespace avoids stale function definitions in the persistent IDA evaluator.
This avoids transferring executable bytes through chat output.

```sh
python3 -m venv build/bds-oracle-venv
build/bds-oracle-venv/bin/pip install unicorn==2.1.4
build/bds-oracle-venv/bin/python tools/bds_reference/generate.py \
  --export build/bds-reference.json \
  --out tests/data/bds_numeric.txt \
  --sine-out tests/data/bds_sine.bin
build/bds-oracle-venv/bin/python tools/bds_reference/collision.py \
  --export build/bds-reference.json --out tests/data/bds_collision.txt
build/bds-oracle-venv/bin/python tools/bds_reference/step.py \
  --export build/bds-reference.json --out tests/data/bds_step.txt
build/bds-oracle-venv/bin/python tools/bds_reference/broadphase.py \
  --export build/bds-reference.json --out tests/data/bds_broadphase.txt
build/bds-oracle-venv/bin/python tools/bds_reference/vertical.py \
  --export build/bds-reference.json --out tests/data/bds_vertical.txt
build/bds-oracle-venv/bin/python tools/bds_reference/water_drag.py \
  --export build/bds-reference.json --out tests/data/bds_water_drag.txt
build/bds-oracle-venv/bin/python tools/bds_reference/water_acceleration.py \
  --export build/bds-reference.json --out tests/data/bds_water_acceleration.txt
build/bds-oracle-venv/bin/python tools/bds_reference/movement_effects.py \
  --export build/bds-reference.json --out tests/data/bds_movement_effects.txt
build/bds-oracle-venv/bin/python tools/bds_reference/rotation.py \
  --export build/bds-reference.json --sine tests/data/bds_sine.bin --out tests/data/bds_rotation.txt
build/bds-oracle-venv/bin/python tools/bds_reference/gliding.py \
  --export build/bds-reference.json --sine tests/data/bds_sine.bin --out tests/data/bds_gliding.txt
build/bds-oracle-venv/bin/python tools/bds_reference/gliding_sequence.py \
  --export build/bds-reference.json --sine tests/data/bds_sine.bin --out tests/data/bds_gliding_sequence.txt
build/bds-oracle-venv/bin/python tools/bds_reference/block_offset.py \
  --export build/bds-reference.json --out tests/data/bds_block_offset.txt
build/bds-oracle-venv/bin/python tools/bds_reference/block_geometry.py \
  --export build/bds-reference.json --out tests/data/bds_block_geometry.txt
build/bds-oracle-venv/bin/python tools/bds_reference/terrain_query.py \
  --export build/bds-reference.json --out tests/data/bds_terrain_query.txt
build/bds-oracle-venv/bin/python tools/bds_reference/unavailable_chunks.py \
  --export build/bds-reference.json --out tests/data/bds_unavailable_chunks.txt
build/bds-oracle-venv/bin/python tools/bds_reference/collision_cache.py \
  --export build/bds-reference.json --out tests/data/bds_collision_cache.txt
build/bds-oracle-venv/bin/python tools/bds_reference/cache_difference.py \
  --export build/bds-reference.json --out tests/data/bds_cache_difference.txt
build/bds-oracle-venv/bin/python tools/bds_reference/cache_refresh.py \
  --export build/bds-reference.json --out tests/data/bds_cache_refresh.txt
build/bds-oracle-venv/bin/python tools/bds_reference/cache_refresh_records.py \
  --export build/bds-reference.json --out tests/data/bds_cache_refresh_records.txt
build/bds-oracle-venv/bin/python tools/bds_reference/sneak.py \
  --export build/bds-reference.json --out tests/data/bds_sneak.txt
build/bds-oracle-venv/bin/python tools/bds_reference/inside_query.py \
  --export build/bds-reference.json --out tests/data/bds_inside_query.txt
build/bds-oracle-venv/bin/python tools/bds_reference/support.py \
  --export build/bds-reference.json --out tests/data/bds_support.txt
build/bds-oracle-venv/bin/python tools/bds_reference/landing.py \
  --export build/bds-reference.json --out tests/data/bds_landing.txt
build/bds-oracle-venv/bin/python tools/bds_reference/finalization.py \
  --export build/bds-reference.json --out tests/data/bds_finalization.txt
build/bds-oracle-venv/bin/python tools/bds_reference/trig.py \
  --export build/bds-reference.json --out tests/data/bds_trig.txt
build/bds-oracle-venv/bin/python tools/bds_reference/native.py \
  --export build/bds-reference.json --out build/bds-native-pages.bin
# macOS: compile for x86-64; Rosetta runs it on ARM. On Linux x86-64, omit -arch x86_64.
c++ -arch x86_64 -std=c++23 -O2 tools/bds_reference/native_oracle.cpp -o build/bds-native-oracle
build/bds-oracle-venv/bin/python tools/bds_reference/block_slowdown.py \
  --export build/bds-reference.json --native-runner build/bds-native-oracle \
  --native-pages build/bds-native-pages.bin --scratch build/bds-slowdown-unicorn.txt \
  --out tests/data/bds_block_slowdown.txt
build/bds-oracle-venv/bin/python tools/bds_reference/relative_movement.py \
  --export build/bds-reference.json --native-runner build/bds-native-oracle \
  --native-pages build/bds-native-pages.bin --out tests/data/bds_relative_movement.txt
build/bds-oracle-venv/bin/python tools/bds_reference/trig_constants.py \
  --export build/bds-reference.json --out build/bds-trig-tables.hpp
cmp src/client/player/BdsTrigTables.hpp build/bds-trig-tables.hpp
cmake --build build --target bds_numeric_selftest bds_collision_selftest bds_vertical_selftest bds_water_drag_selftest bds_movement_effects_selftest bds_gliding_selftest bds_gliding_trace_selftest bds_trig_selftest bds_finalization_selftest bds_landing_selftest bds_support_selftest bds_world_collision_selftest bds_terrain_query_selftest bds_block_geometry_selftest voxel_shapes_selftest bds_block_offset_selftest block_offset_definition_selftest bds_collision_cache_selftest bds_cache_difference_selftest bds_cache_refresh_selftest bds_cache_refresh_records_selftest bds_block_slowdown_selftest bds_sneak_selftest bds_inside_query_selftest physics_selftest bedsim_trace_selftest
ctest --test-dir build -R '^(bds_numeric_selftest|bds_collision_selftest|bds_vertical_selftest|bds_water_drag_selftest|bds_movement_effects_selftest|bds_gliding_selftest|bds_gliding_trace_selftest|bds_trig_selftest|bds_finalization_selftest|bds_landing_selftest|bds_support_selftest|bds_world_collision_selftest|bds_terrain_query_selftest|bds_block_geometry_selftest|voxel_shapes_selftest|bds_block_offset_selftest|block_offset_definition_selftest|bds_collision_cache_selftest|bds_cache_difference_selftest|bds_cache_refresh_selftest|bds_cache_refresh_records_selftest|bds_block_slowdown_selftest|bds_sneak_selftest|bds_inside_query_selftest|physics_selftest|bedsim_trace_selftest)$' --output-on-failure
```

The oracle executes x86-64 code using the Windows ABI, MXCSR `0x1f80`, and bounded
instruction counts. Friction handles use synthetic valid/absent ECS membership.
Sprint-jump verification executes the whole numeric basic block with the BDS
generated table, ending before ECS bookkeeping; it does not emulate a full jump
system or substitute external calls. Ground-acceleration runs use a synthetic
world-query callback which returns the supplied block and records sampled
coordinates; the actual BDS CFG dispatch trampoline executes. The fixture's
identity cases describe hash equality, not independently verified block names.
Fixtures contain numerical outputs and
function hashes, never extracted code. Ordinary CTest runs need neither IDA nor
Unicorn. Most inputs cover finite values. The newer trig/relative-movement
suite includes NaNs, infinities and subnormals, but their behavior in other
kernels, alternate rounding modes and actual process startup floating-point
configuration remain to be audited.

The current suite checks 69,318 cases (3,782 sampled numeric cases plus 65,536
sine-table entries) with zero differing fields on macOS arm64. It also passes an
`-O3 -ffp-contract=fast` build. Windows/x86 and other production platforms still
require verification. Regenerating the fixture files from the local export is
deterministic.

The collision suite adds 4,708 cases with zero differing fields. Contact and
sequential movement execute complete BDS functions, including the actual stack
cookie check. Recovery updates supply an existing ECS tag through a synthetic
registry lookup; no allocation/creation behavior is claimed. The local player
now keeps recovery state through rewind/replay; history/custom-minimum inputs
are represented but their BDS producer systems have not been ported. The
combined numeric/collision count is 74,026, still not a full-physics accuracy
denominator.

The vertical suite adds 3,744 cases with zero differing fields, bringing the
combined sampled-kernel/table count to 77,770. Synthetic preexisting ECS pools
supply gravity and levitation components. Chained execution preserves heap
state between the actual BDS callbacks, beginning with velocity and fall
distance supplied by the fixture. The water cases begin **after water drag**.
The callback order follows registration `0x1402CECC0` and query exclusions;
this is not execution of the complete ECS scheduler. Levitation effect 24 and
slow falling effect 27 come from the BDS effect initializer `0x143874B00`.
Ground/air vertical-drag queries `0x141063E80` / `0x141063D00` exclude auto-climb
and discard-friction tags but do not exclude levitation. Perlin does not yet
produce the discard-friction tag. FallDistanceComponent identity follows
metadata builder `0x14106A430` (hash `0x81125CB5`); only its reset paths are
currently ported. Native x86 execution here still does not prove whole-tick or
cross-platform agreement.

Water drag adds 4,320 cases, for 82,090 bounded numeric/table cases in total.
Its oracle executes both the complete drag callback and BDS's actual maximum
level virtual method. The registry initializer identifies enchantment 7 as
depth strider; the method returns 3 for that ID. No service return value is
substituted. Optional component handles use synthetic sparse pools. Perlin's
ordinary armor inventory supplies the maximum enchantment level, as traced
through producer `0x140DF7F90`, lookup `0x143883AC0` and item enchantment lookup
`0x143C073E0`. The ActorEquipmentComponent factory `0x143881890` and insertion
`0x143883A30` establish the armor container at offset 8; vtable `0x146741338`
slot 25 enumerates it through `0x14339C9A0`. BDS reserves five armor slots;
Perlin currently represents four player armor slots and does not yet handle
the fifth body slot. Optional WaterMovement drag is represented in Attributes
with an absent default, but its full producer systems remain unported.

Water acceleration adds 6,912 cases. The native player lookup distinguishes
stale registry generation, matching generation with absent PlayerComponent,
and matching player membership. All produce the same results as the port's
resolved membership selection. Attribute cache updater `0x141002AE0`, reached
through UpdateAttributesSystem factory `0x141002D20`, vtable `0x1466D4A90`, run
`0x14101F450`, loop `0x141010780` and callback `0x141010770`, copies current
attribute values into optional fields. Map builder `0x140D48B40` and attribute
initializers `0x1400AA930` / `0x1400AAB10` establish movement at offset 0 and
underwater movement at offset 8. `0x1435800E0` sets the ordinary underwater
attribute to `0.02f`. Perlin preserves received zero values and clears its
attribute cache on reconnect; it still needs the complete local attribute
modifier/prediction lifecycle rather than assuming received values are
sufficient for every transition.

Movement-effect tests add 1,296 expiry cases, 288 swim modifier cases and 648
refresh/application cases. The combined bounded-kernel/table count is now
91,234, with zero mismatching fields. The apply helper is executed with null
notification/packet contexts and preallocated two-slot storage, avoiding
allocation substitutions. Tests include signed integer wrap and infinity
sentinels; the source tick is preserved until expiry. BDS post-player-movement
registration `0x1402D5190` places MovementEffectsTick after movement. This
provides phase evidence, not execution of the full native scheduler. Perlin's
physics wrapper applies received events before movement and expires them
afterward, and input/state snapshots preserve both through replay. Packet
arrival alignment with its server tick still needs independent multi-tick
verification.

The BDS enum registration `0x1413F4830` names movement effect 0 GLIDE_BOOST and
1 DOLPHIN_BOOST; AddDolphinBoost callback `0x140B54840` supplies duration 60 and
type 1 to the common helper. Perlin's generated 1.26.51 protocol currently
names value 1 GEYSERBOOST. This port uses the audited 1.26.10 numeric values;
cross-version semantic agreement is unverified. Glide effects now feed the
verified glide callback described below. Dolphin proximity scanning and
prediction are still missing. Neither the kernel fixtures nor the integration
checks establish end-to-end packet or complete multi-tick BDS parity.

Direction/gliding tests bring the bounded kernel/table count to 92,784 cases.
The rotation helper wraps and interpolates both angles even at alpha 1, then
uses the verified sine table. The gliding callback has a separate radians-to-
table conversion for its lift calculations, and its operation order differs
from the old formula. Slow falling changes both gravity coefficients even when
ascending. An active glide boost blends all three velocity lanes before drag.
Fall distance becomes 1 when the incoming vertical velocity exceeds -0.5.
The optional effect handle is tested as absent, empty, inactive and active.

The direction routine imports `fmodf` through the CRT thunk at `0x146254354`.
Its external implementation is absent from the IDB. The oracle supplies only
that IEEE remainder service with host fmod on binary32 arguments, while BDS
executes all wrapping, interpolation and direction arithmetic. This dependency
is explicitly recorded in the fixture; it is not a claim of native CRT parity
on exceptional inputs. A separate real BDS sprint block tests table-index
conversion for large yaw, infinities and NaN payloads, avoiding undefined C++
out-of-range float-to-integer casts. Other non-finite physics behavior remains
unverified.

Three 96-tick native sequences preserve heap state across effect application,
the gliding callback and expiry. They supply changing rotations, slow-falling
transitions, short/long refreshes, infinite boosts and expiry boundaries. The
integration test runs Perlin Step in an empty world and compares velocity,
fall distance and the full glide-effect record at every tick, with exact
agreement on all 288 ticks. Positions and collision output are not oracle
outputs in these sequences. Perlin preserves its previous tick rotation in
State for rewind/replay; equality to BDS's full ActorRotationComponent history
producer is not established by supplying the same synthetic history to both.

The full local macOS arm64 build succeeds, including Perlin and trace_diff,
after integrating upstream `main` at `084a657`. The metadata merge keeps
integer flag parsing and known-word tracking with the upstream helper API.
The latest full CTest run passes 52 of 53 tests; the sole failing test is the
legacy trace disagreement described below; the new web disagreements are resolved. The pure direction/glide
suite also agrees under `-O3 -ffp-contract=fast`, and when relaxed math options
are followed by the physics source's strict overrides. These checks do not
substitute for Windows/x86, other platforms or real game/server validation.

Bundled trig and relative movement add 10,832 cases, bringing the kernel/table
count to 103,616. Move finalization adds 1,792, bringing the total to 105,408. Landing response adds 1,856, and supporting-block selection adds 1,280,
for 108,544. Unavailable-chunk collision collection adds 564, for 109,108. Terrain traversal adds 382 and the common block collector adds 310, for 109,800. Position seeding, offsets and description conversion add 1,388, for 111,188. Collision-cache append, subtraction and refresh add 1,593, for 112,781. Native request initialization and 256 nonempty cache refresh callbacks add 257, for 113,038. Block slowdown callbacks add 2,304, for 115,342. Sneak filtering and cached-shape clipping add 1,024, for 116,366. Entity-inside traversal and region readiness add 1,316, for 117,682. Complete ordinary and alternate ground-jump callbacks add 1,536, for 119,218. Jump request classification adds 1,296, for 120,514. Ground-jump permission adds 2,048, for 122,562. Honey contacts add 640, for 123,202. Seven native sprint impulses observed during independent legacy-trace replay bring this to 123,209. Fluid-state, finite-bounds and common liquid-collector executions add 4,256, bringing the current total to 127,465, plus the separately reported 288 chained glide ticks. The
trig inputs include all exponent classes, mantissa boundaries, both signs,
small/large reduction thresholds, infinities, quiet/signaling NaN payloads and
seeded random float bit patterns. Both BDS SSE2 and SSE4 implementations agree
on these inputs. The portable scalar trig retains the actual sincos wrapper's
rounded `abs(angle) + pi/2` cosine argument, rather than calling host cosf.
The large finite path uses 16-bit partial products and 32-bit wrapping carries,
then the BDS reconstruction coefficients. Only numerical coefficient words are
checked in; executable pages remain under build/. Their extraction is
reproducible with hashes through trig_constants.py.

The optional native oracle loads hash-verified BDS pages into a separate x86-64
process and calls the original routines using the Windows ABI. It does not
modify their instructions or substitute math calls. Native x86-64 execution
under Rosetta agrees with all 7,248 emulated trig records and every finite
relative-movement output. It disagrees with Unicorn 2.1.4 on 72 NaN payload
fields in relative movement: the emulator can choose a different NaN operand
than actual SSE arithmetic. The relative fixture therefore requires native
reexecution; its generator verifies unchanged inputs and exact agreement on
all non-NaN outputs before retaining native results. The fixture records this
discrepancy explicitly. Perlin's scalar SSE-result helpers preserve destination
NaN payloads and the negative canonical invalid-result NaN on ARM as well.
This is result-bit verification, not reproduction of floating-point exception
flags. Other kernels' exceptional values remain unverified.

The new suite passes optimized ARM64 and x86-64/Rosetta builds of Perlin's
portable code, including `-ffp-contract=fast`. It still does not establish
Windows process behavior, the complete floating-point environment, world/input
selection, positions, collision ordering, network authority or whole-tick parity.

The complete auto-step callback uses host allocation/memmove/free and finite
float-classification services; its movement arithmetic and bounded-position
validation run as BDS instructions. The filter oracle supplies tag storage and
insertion/removal, then records BDS's decision. No full ECS lifecycle claim is
made. The broad-phase oracle records the query region and asserts that the BDS
cache refresh stores exactly those bounds. Its world returns no shapes, so it
does not establish query ordering or block-specific collision generation.

The callback tests exposed a compiler contraction bug: with fused arithmetic,
the same two horizontal components could compare unequal in opposite squared
sum orders and cause a spurious step. Binary32 multiplication barriers fix the
acceptance comparison even under `-O3 -ffp-contract=fast`. PlayerPhysics.cpp
also compiles with contraction disabled (`/fp:strict` on MSVC) to preserve the
reference's separate scalar operations throughout the tick. Unix builds also
disable fast math for this source, preserving signed zero and real division
even if PERLIN_FAST_MATH enables relaxed math for the rest of the application.

The filter's MovementAbilities bit 32 is the value of **Flying**, rather than
an unidentified extra stepping ability: `UpdateAbilitiesSystem` factory
`0x1410086F0`, vtable `0x1466D4B70`, wrapper `0x14101FAC0`, loop `0x141010E10`
and callback `0x141010E00` reach updater `0x140D489F0`. Its map initializer
`0x1400801A0` maps source ability 9 to destination value bit 0 at offset 4.
Ability-name pointer table `0x147130620` identifies ability 9 as `flying`.
Step supplies the locally predicted flying state to this eligibility branch.

## Next pipeline anchors

Gravity registration `0x14103EC40` contains `MobGroundGravity`, `MobAirGravity`,
water/lava variants, `ApplyGravity` and component cleanup. The port now uses the
verified ground/air and player-water callbacks described above; mob-water/lava
callbacks `0x14103E360` / `0x14103E580`, full selection/cleanup and scheduling
remain under audit.

Water drag callback `0x14103D110` is reached through `0x141052350` and loop
`0x1410523B0`, with query builder `0x1410640F0`. String/hash metadata identifies
the inputs as ActorDataFlagComponent (`0x33475976`), SwimSpeedMultiplierComponent
(`0x7A66A25B`), WaterWalkSpeedEnchantComponent (`0x07948AF9`), StateVectorComponent,
and WaterTravelFlagComponent (`0x0F8AF088`), plus optional WaterMovementComponent
(`0x4FC426B5`) and OnGroundFlagComponent (`0xC3E299A3`). The drag arithmetic and
ordinary armor enchantment source are now ported. Water travel callback
`0x140F1E690` now supplies verified acceleration arithmetic, with descriptor
sources established as described above. Full modifier, travel-tag and
attribute-update ordering still needs reconstruction.

SwimControlSystem registration `0x1402CC730` builds definition `0x140F54BD0`,
metadata callback `0x140F596C0` / `0x140F6E490`, and executable vtable
`0x1466D3410` → run `0x140F4FC30` → loop `0x140F54910` → callback `0x140F4FB60`.
The callback is located and uses the rotation direction helper, flying ability,
MoveInput bit 2 and PlayerInputRequest bytes 32/33. The request-field producers
still need semantic identification before replacing the current eligibility
logic. The existing look calculation therefore remains unported.
RotateAndSetVelocitySystem registration `0x140EF0D90`, vtable `0x1466D2398`,
run `0x140EFB5C0`, loop `0x140EF0BD0` and callback `0x140EF0BB0` copy two
already-computed lanes; their producer and the full rotation arithmetic remain
to be traced. DefaultMoveSystems registration `0x140B52880`, called from
`0x1402CE3E0`, uses Ground/Air vtables `0x1466C47A8` / `0x1466C47E0` and shared
callback `0x140B52740`. Its movement helper `0x140B52390` calls the common
relative-motion kernel `0x1432674C0`. Unlike the direction/sprint helpers, this
kernel calls bundled sincosf `0x146251E30` (which packs sin/cos arguments and
dispatches to vector sinf); it does not index the sine table. The shared kernel
and trig are now ported. The surrounding ground/air/flying queries, default
movement side effects, coefficient producers and complete scheduler integration
remain to be reconstructed.

`TravelMoveRequestSystem` factory `0x1410005E0`, vtable `0x1466D5050`, wrapper
`0x141011450`, loop `0x1410004A0`, callback `0x141000420` copies state and adds
velocity into MoveRequest. `MoveCollisionSystem` callback `0x140F90EA0`, reached
through vtable `0x1466D3C68`, now supplies the verified search-region and cap
arithmetic in Step. Cache append/subtraction/refresh are now ported; complete world-query adapters and request lifecycle remain under audit.

Variable max-step systems are registered by factory `0x140F1B030`, with client
and server vtables `0x1466D2DE0` / `0x1466D2E18`. Their producers, definition
component and client/server differences remain under audit. `Attributes` now
accepts a max-step height, but the full variable-height producer is not ported.

Move finalization uses synthetic ECS sparse pools and mechanical tag storage;
the callback and its tag-decision helpers execute unchanged. The only replaced
runtime services are pool lookup, insertion/erasure storage and finite position
classification. This does not validate the real registry implementation. Cases
include all 32 previous tag combinations, three ability memberships, both values
of MoveRequest byte 136, exact epsilon boundaries, sub-epsilon clipping,
nonzero offsets, varied bounds and seeded random inputs. Portable results agree
on ARM64 and x86-64/Rosetta. Full Step checks separately cover wall impact with
preserved horizontal motion, no-clip flag retention, zero-motion flying ground
retention, nearby-tag clearing and capped displacement with uncapped velocity.

The abilities map initializer `0x1400801A0` inserts ability 17 with mapped index
4 (20-byte entries inserted by `0x140D575E0`). Name table `0x147130620` names
ability 17 `noclip`. Setter `0x14100E0D0` writes boolean index 4 into the second
32-bit word, confirming component bit 36 used by finalization. Cache converter `0x140F90C90` resets MoveRequest byte 136, then sets it when
any incoming 48-byte collision record has a nonzero byte at offset 44, including
records with null block pointers. Prepass `0x143EA1990` identifies one producer: unavailable-chunk walls have
record byte 44 set to one. The terrain loop in `0x143E9FFF0` writes zero.
Perlin derives the preservation flag from these walls; the temporary input and
per-block overrides have been removed. Other producers remain under audit. The callback's
position validator rejects invalid coordinates; the portable helper's contract
requires valid bounds/offsets and does not recreate BDS diagnostics. No-clip's
complete movement preparation/cap and world query order remain unverified.

The landing oracle executes `VerticalCollisionSystem` callback `0x140F1D820`
through supplied block descriptors. BlockLegacy constructor `0x143F360E0`
installs vtable `0x1467BA4A0`, whose slot 65 is ordinary handler `0x143F3BF60`.
Bed/slime constructors `0x1406F1CF0` / `0x14069B3E0` install vtables
`0x1467B4238` / `0x1467F4C38`, with handlers `0x143F0ECA0` / `0x144282EC0`.
The named bed/slime event subscription lambdas establish those class identities
independently (`0x143F17A00` / `0x14428ADA0`, RTTI descriptors `0x14722EFD0` /
`0x14724E4D0`). The vertical query builder `0x140F23140` requires AABB,
MoveRequest and VerticalCollisionFlag components; it does not require OnGround.

Native visitor `0x140F22ED0` tests ActorDataFlagComponent bit 1 (sneaking).
Visitor `0x140F22DD0` passes the actual negative StateVector vertical velocity
and the block's coefficient to helper `0x140F1D190`. This creates or updates
ApplyRestitutionComponent (hash `0x76E7A1DF`), preserving its X/Z fields and
writing Y as `-(velocityY * coefficient)`; MinecartFlagComponent multiplies
that result by `0.95f`. Coefficients are `0.75f` for bed and `1.0f` for slime.
There is no `0.08` minimum speed. Both handlers clear vertical velocity after
requesting restitution. With both BounceComponent and PlayerComponent present,
the native helper also stores floored actor coordinates and the block pointer
in BounceComponent. The oracle checks those writes and absent-component cases,
and executes the separate restitution addition callback. Runtime storage hooks
only supply world lookup, ECS pools, a query sentinel and component insertion;
these services use explicit return stubs. None of the tested block decisions,
visitors, membership checks or arithmetic is replaced by expected values.

Perlin now keeps pending restitution in prediction state, adds it before drag
and clears it after application. Step regression tests cover low-speed bounces,
negative velocity above the displacement cap, crouching, flying and consuming
a pending impulse without vertical collision. Its supporting block now comes from the native selector over the collected
shapes, carrying parallel material metadata. The collected candidate set and
its ordering/lifecycle still differ from the fully reconstructed native cache. Native bounce component creation/consumption, actor position offset
conversion, all other block overrides, block scripting events and full phase
selection/order remain required for the complete requested port.

The supporting-block selector lowers the query minimum Y by `0.2f`, sets
maximum Y to the same plane, and computes horizontal center using float extent
halves. It scans cached shapes in forward order, preferring the smallest
nonnegative difference between that plane and each shape's center Y. Equal
vertical distances use squared center distance; exact ties retain the first
entry. Horizontal intersection is not a selection condition. Only the winning
shape is checked for strictly positive dimensions; an invalid winner returns
an empty result without trying another shape. A valid winner returns floored
minimum coordinates and its parallel block pointer, which can itself be null.
The fixture includes both orders of ties, empty sets, null metadata, invalid
winners, nonzero/large positions and seeded random boxes. The Step edge-landing
regression places the center over air while the body overlaps adjacent slime.
A separate integration case checks horizontal-motion preservation when a
nearby unavailable-chunk record accompanies an ordinary wall collision.

Unavailable-chunk prepass `0x143EA1990` scans chunk coordinates from
`(floor(minXZ) - 8) >> 4` through `(floor(maxXZ) + 8) >> 4`, inclusive. X changes
fastest, then Z. A chunk is unavailable if its pointer is null, its packed
position at byte 120 equals `0x8000000080000000`, or bytes 130 or 5352 are nonzero.
Each wall covers the chunk's full 16-by-16 footprint and Y from `-100000.0f` to
`100000.0f`, with null block metadata, zero block coordinates and preservation
flag one. These records are appended before terrain collection when the query's
prepass flag is enabled. The fixture substitutes only the chunk lookup and
supplies preallocated output storage; native traversal, predicates and record
writes execute unchanged. It covers all four unavailability conditions, ready
chunks, mixed states, boundaries around negative/positive chunks, large
coordinates, zero/inverted extents and seeded random boxes.

Perlin now collects these walls for movement and collision probes, preserving
their native order before terrain shapes, and skips block collection in missing
columns. The client provider currently maps `Level::HasChunk` to ready/missing;
it has no established equivalent for the other three native availability checks.
The Step regression checks collision above the stored block height, preservation
of horizontal velocity at the wall, and release after the chunk becomes ready.
The exact query-call predicates, dimension-specific limit production, subchunk
readiness, world-border queries and native cache refresh/lifecycle are
still required for the complete world collision port. The base terrain query
and prepass have been located; their full adapter path remains under audit.

Terrain query `0x143E9FFF0` acquires columns with X outermost and Z next, then
visits each acquired column in ascending Y. Expanded cell limits use float
subtraction/addition of one before floor conversion. Minimum Y is clamped to
the source minimum height and maximum Y to the source maximum. If minimum Y
is greater than or equal to maximum Y, column traversal is skipped; otherwise
both endpoints are visited. Missing columns skip the entire Y loop. This
preserves a distinction between the native chunk-availability prepass and
column acquisition (virtual slots 320 and 312, respectively).

The query also emits a null-block boundary when the source minimum height is
greater than the query maximum Y. Its X/Z bounds equal the query, minimum Y is
negative FLT_MAX, and maximum Y is the source minimum minus 40, converted through
a signed 16-bit integer. Native accessors `0x143E8F460` / `0x143E8F450` read the
source's 16-bit fields at offsets 58/56, confirming that the clamp and boundary
use the same minimum. The fixture checks ordinary/custom heights, zero height,
both signed 16-bit extremes, below/inside/above queries, missing/mixed columns,
large horizontal coordinates and seeded random extents. It executes the full
terrain query and world-border tail; host services supply empty block geometry
and a true native world-border skip predicate. This does not establish block
geometry or world-border behavior.

Perlin's movement and collision probes now use this traversal. LevelCollision
currently supplies the stored chunk range (`-64` through `320`); generic test
worlds default to the signed 16-bit range and can override it. Actual per-
dimension limit production and matching subchunk lifecycle remain unported.
BlockShape now carries a variable-length collision-box vector; the previous
eight-box cap is removed. Reproducing every geometry/offset producer remains required.
An integration regression verifies that falling through an empty world with
minimum height `-64` stops at the native boundary height `-104`.

The common block collector `0x143F386B0` checks the collision component pointer
at block offset 112. A present component resolves an offset through accessor
`0x143EECEF0`, floors each lane, adds the integer block position with 32-bit
wrapping arithmetic, converts that origin to float, and translates each local
float box. Enabled components iterate the full variable-length array in order;
disabled components return false. Emission requires strictly positive dimensions
and, if a query exists, strict overlap in every axis. Touching alone is excluded.
An enabled component returns true even if it emits no boxes. Without the
component, the block's virtual slot 5 supplies a world AABB; the collector tests
and optionally emits it, preserving its distinct return behavior.

The fixture executes the actual collector with a supplied offset accessor and
fallback AABB method, using preallocated output storage. These are explicit
services, not evidence for the producers they replace. Cases include arrays of
0, 1, 8, 9, 16 and 32 shapes, both query modes, negative/nonintegral offsets,
nonzero/large block positions, invalid dimensions, edge-touch queries and seeded
random shapes. Step uses the same verified translation and filtering functions
with its existing local-box provider. Actual offset/component production and
all block-specific overrides are still required. Offset accessor `0x143EECEF0` and its generator `0x1441FC5A0` are located.
The generator is now ported, including position seeding, 64-bit random state and
per-axis ranges/step counts. Component lookup through the accessor remains unported.

Removed fixed-size BlockShape storage and updated its producers, render/query
consumers, trace adapters and entity-box accumulation to retain every shape.
VoxelShapeRegistry no longer merges overflow boxes into bounding volumes that
fill separating gaps. A packet-data regression checks 16 disjoint occupied
voxels and every separating empty voxel; this is a geometry-preservation test,
not an independent BDS oracle. The Step regression places the sole blocking box
at index 31 and confirms it participates in collision. Source block geometry
assets, greedy voxel decomposition/order and native entity geometry remain under
audit; retaining all boxes does not establish those producers' equivalence.

Position seed constructor `0x14048C340` combines a wrapped signed 32-bit
X product, signed 64-bit Z product and signed Y, then hashes and sign-extends a
truncated 32-bit result before mixing two 64-bit state lanes. All integer
arithmetic in the port uses explicit unsigned wrapping and bit-preserving signed
conversion. Generator `0x1441FC5A0` explicitly replaces Y with zero before
seeding. It derives three 24-bit float draws using 64-bit additions, rotations
and XORs. Each axis independently returns its minimum if the range is not
increasing; steps zero select a continuous range, one selects the midpoint,
and larger counts select a floored discrete index. Draws still advance for
constant/midpoint axes. The operation order, division and float rounding are
preserved, including signed zero, unsigned step-count extremes, subnormal output
and generated invalid-result NaNs for overflowing test ranges.

The offset fixture executes the complete seed and generator routines without
math services. It covers coordinate boundaries and seeded full-int32 positions,
Y-independence in block offsets, increasing/constant/reversed ranges, step counts
from zero through UINT32_MAX, tiny ranges and finite extremes. Description
producer `0x1441FC8B0` is also executed with initialized synthetic TLS and a
mechanical component-constructor capture. It multiplies the six range lanes by
`0.0625f` and copies three step words when its destination context is writable.
These 18 producer cases validate conversion and constructor arguments, not the
TLS initialization, registry or schema loader. Schema registration `0x1441FC9D0`
identifies `random_offset`, axes `x/y/z`, and the per-axis `range` and `steps`
properties; accessor type-ID references connect this component to the generator.
FloatRange registration `0x144D35800` names its `min`/`max` members. Description
constructors `0x14425E650` / `0x1442CAB50` zero all three ranges and step counts;
range validator `0x14422BD70` rejects maximum below minimum.

BlockShape now optionally carries a resolved offset component. Step generates
its position-specific offset and passes it through the verified collision-origin
floor/translation path. A regression confirms that a shifted partial box changes
collision contact. The custom client block provider now populates the optional component from
canonical descriptions. Vanilla component lookup, complete loader constraints,
missing-field acceptance and native permutation semantics still require
reconstruction before claiming full block offset support.

Canonical custom definitions now load `minecraft:random_offset` through
CustomBlocks → ParseBlockOffsetDefinition → ServerBlocks → LevelCollision.
The parser reads the native registered `x/y/z`, `range.min/max` and unsigned
`steps` fields, applies the verified 1/16 conversion and carries the component
alongside geometry. It rejects malformed field types, nonfinite/unrepresentable
ranges, reversed ranges and invalid unsigned step values. Omitted data is zero-
initialized, matching native description constructors; native loader acceptance
and diagnostics for every omitted/malformed form are not established. Base and
permutation offsets are stored independently of collision geometry so an
offset-only override retains the existing geometry. Matching permutations use
Perlin's existing condition evaluator/order, whose full native equivalence
remains unverified. Neighbor-dependent geometry also retains offset metadata.

The loading regression reconstructs canonical description objects from 959
representable BDS offset cases, parses and stores them, then compares generated
outputs to the existing independent BDS fixture. This reuses reference cases;
it does not increase the independent kernel-case count or execute BDS's schema
loader. Complete NBT/schema loading, cross-component constraints (including
collision bounds/full-block restrictions), vanilla definitions, component
removal, native permutation behavior and real client/server checks remain.

Collision definitions and voxel packets now have source revisions. Cached
LevelCollision entries rebuild when either changes, with revision rechecks
before publication. Older immutable snapshots stay alive until shutdown because
ShapeOf returns references used by renderer/query callers. Registry tests check
revision changes and definition reset; concurrent publication and the complete
world/definition lifecycle still need integration coverage. The definition
parser compiles with strict float flags after relaxed target flags, as does
PlayerPhysics, preserving conversion and nonfinite checks.

## Accuracy reporting and conflicting legacy evidence

Bit-exact agreement on these sampled numeric inputs and the exhaustive bounded
sine table is not a percentage of full physics parity. Overall accuracy is
currently **unmeasured**. A final percentage must state the independent BDS
scenario denominator, component/branch coverage, tolerance policy, transition
coverage and all unverified paths. Passing a scenario subset cannot establish
the requested full equivalence.

`bedsim_trace_selftest` predates this audit. Its reference provenance is not
established, it has named scenario exclusions, and it does not check every
parsed field. Before slowdown integration, 152 of its 5,400 checks disagreed
across four scenarios (`gen:terrain1_1`, `gen:floors1_0`, `gen:ladder1_0`,
`gen:effects1_0`). The first slowdown integration added 96 disagreements in `gen:web1_1`,
for 248 failed checks across five scenarios. Tracing the native sneak callback
then exposed an old adapter error: it copied the clipped request into stored
velocity, undoing the earlier slowdown clear. Using the native clear-only
callback behavior resolves all 96 web differences; the latest result is again
152 failed checks across the original four scenarios. Full contact selection,
timing and integration still require independent BDS scenario evidence. Keep all failures
visible until the original scenarios have an independently established BDS
oracle. Do not loosen their tolerances, silently regenerate expectations from
Perlin, or count their results as BDS accuracy.


## Collision-cache evidence and lifetime limits

The converter appends both parallel vectors, resets preservation from incoming
records alone, clears metadata for null block handles, and overwrites the cached
region. Empty incoming records can therefore clear preservation while retaining
old shapes. Contained refreshes retain the old region and flags.

Region subtraction follows the native ordered worklist (at most 27 points),
`0.00001f` split offset, `FLT_EPSILON` point deduplication and `0.001f` volume
merge tolerance. Native merge comparisons combine relative candidates with
already translated prior output before translating a newly appended candidate;
the port preserves that coordinate order, including nonzero-origin cases.
The oracle hooks vector allocation/append, free and memset only; all difference
arithmetic and branching execute the original BDS instructions. Regenerating the
subtraction fixture after sharing storage hooks produced identical bytes.

Refresh fixtures execute the complete callback with an empty synthetic world,
two retained shapes and metadata, and varied prior regions. They verify capped
motion, query sequence, final bounds, flags and retained entries. An additional 256 executions now run the entire callback with nonempty
synthetic world responses, checking retained and appended shape/metadata
vectors, null block handles, all preservation flags and query order together.
The actual world adapter, actor-shape membership and allocation-growth paths
remain outside this oracle. All 1,593 new cases pass both optimized ARM64 and
x86-64/Rosetta portable builds.

RemoveFromAllEntities<MoveRequest> factory `0x1403086E0`, vtable `0x14669CC20`
and run wrapper `0x1403291B0` reach pool hash `0xE3E1BB89`. Registration in
`0x1402CCE40` follows PlayerPostMoveSystem and is conditional on a flag byte.
The flag meaning and complete phase schedule remain untraced. Step currently
uses a fresh request cache per movement call; unconditional native per-tick
removal or equivalent multi-request lifetime has not been established.


Native request construction `0x140355790` copies the invalid AABB at
`0x14740E9A0`: all six lanes contain `0xFFFFFFFF`. Its full 152-byte initialization
was executed with synthetic sparse-pool insertion/storage services; both bounds
arrays contain that sentinel, with the remaining fields zero. The cache now
uses these exact initial region bits. The full refresh oracle includes 32
fresh-sentinel cases with zero through three incoming records.

This uncovered an unordered-comparison error in the first portable refresh.
At `0x140F91452` and `0x140F91461`, COMISS/JBE takes the full-query branch for
NaN old bounds. Treating those as ordered `<=` comparisons incorrectly called
subtraction, introducing round-trip changes in several query bounds. The port
now preserves the actual asymmetric branch directions, including containment
upper bounds. All 256 new callback cases and the constructor region match on
optimized ARM64 and x86-64/Rosetta. Regeneration produces identical fixture bytes.

Additional lifecycle evidence: `0x143260A80` writes requested movement then
executes the phase identified by `word_147290A74`. `0x1402CCE40` tests whether
that phase exists to select its removal registration. Minecart registration
`0x1402D1DA0` calls the shared movement registration and then explicitly registers
RemoveFromAllEntities<MoveRequest>. This is evidence of multiple phase contexts,
not proof of the complete local-player request lifetime; the phase's full
identity, callers and execution schedule still require tracing.


## Block slowdown behavior and integration limits

`BlockMovementSlowdownMultiplierComponent` is hash `0x701222D1`,
`StateVectorComponent` is `0x0EC93C91`, the applied record is `0xDDCED0C5`, and
fall distance is `0x81125CB5`. The apply callback activates when any multiplier
lane compares ordered nonzero with zero; subnormals activate it, signed zeros
and NaN-only vectors do not. It multiplies requested movement by the multiplier,
then clears StateVector velocity and all multiplier lanes before collision.
The former squared-length threshold and velocity clear after restitution were
incorrect. Regression checks exercise subnormal activation and retention of a
pending restitution impulse after slowdown.

The contact accumulator's threshold is per-lane `FLT_EPSILON`, including equality.
On the first contact it records whether no existing lane meets that threshold;
subsequent contacts keep the record's original value. Existing multipliers use
ordered component minima; otherwise incoming bits are copied directly. Native
COMISS/JNB makes unordered values fail the threshold, which matters for both
the constructor and accumulator. The fall-distance callback writes positive zero
when the applied byte is nonzero, preserving all input bits otherwise.

Web membership is established by the view builder `0x140B64890`; its shared
contact callback uses the triplet at `0x1468A10F8`: `(0.25f, 0.05f, 0.25f)`.
Registration `0x1402D48A0` orders EntityInside, exemptions, optional resistant-mob
handling, fall-distance adjustment, and applied-record cleanup. Cleanup factory
`0x140C1F290` reaches `0x140C263F0`, which names removal of the applied component.
Player immunity `0x140C1EE80` requires MovementAbilities value bits 0 and 2;
updater map `0x1400801A0` and ability-name table `0x147130620` identify these as
flying and instabuild. Step maps these to predicted flying and creative mode,
clears the multiplier and suppresses first-contact fall reset when both hold.
The immunity fixture executes the original callback and clear helper with an
absent applied record; real sparse-pool erasure remains outside that fixture.

The 1,024 application cases run without runtime hooks in Unicorn and again as
original BDS code under x86-64/Rosetta with MXCSR `0x1F80`. The native run corrects
three emulator NaN-payload fields; no finite output differences were accepted.
Accumulation fixtures supply mechanical applied-record lookup; absent records
are initialized by the original constructor with insertion/storage hooks. The
full 2,304-case portable suite matches optimized ARM64 and x86-64 builds, and
fixture regeneration produces identical bytes.

Runtime contact scanning now uses the separately verified native inset/cell
traversal and region readiness. It retains Perlin's web identity. Native
EntityInside block-specific shape predicates, dimension/world access,
other slowdown sources (including berry bushes), spider/wither immunity and
resistant-mob feature gating remain unported. The verified callbacks do not
establish whole-tick or complete web physics parity.


## Sneak movement and cached-shape clipping

The native filter `0x140E76AD0` checks two sparse-pool memberships/generations.
When the optional 100-byte control component is present, its first byte bit 0
supplies the crouch condition; otherwise ActorDataFlag bit 1 does. Ground
membership is required. It writes MoveRequest byte 85 before rejecting actor
flag bit 6. Wrapper `0x140E8D2F0` identifies the optional view as MoveInputComponent
(hash `0x468BCD2E`) and the other view as OnGroundFlagComponent
(`0xC3E299A3`). The native enum-name table `0x146AFBD80` identifies actor
flag bit 6 as TEMPTED. Step supplies its local sneak/ground state and now
records the low actor-flag byte from own-player metadata in replayable Input.
The full MoveInput producer and scheduler remain under audit.
The native filter itself is tested for absent, valid and stale memberships.

The complete kernel in `0x140E76C70` uses cached shapes collected before this
system, not fresh world probes for each iteration. It insets X/Z by `0.025f`,
collapses inverted inset axes to the original center, uses max-step times
`1.01f` as the vertical probe drop, and reduces X, then Z, then both lanes
in `0.05f` steps with native rounding and clamp order. Only horizontal velocity
lanes whose final request is within `FLT_EPSILON` of zero are cleared. It never
copies the clipped request into StateVector velocity. Y movement/velocity remain
unchanged. Step now follows this cache/clip/resolve order and retains a separate
stored velocity throughout, fixing the slowdown/sneak interaction.

The 1,024 reference cases execute the full kernel and filter without runtime
hooks, using synthetic sparse pools and provided shape arrays. They include
nonzero positions, thin/inverted body bounds, multiple support shapes, zero and
negative max-step values, signed-zero and epsilon boundaries, independent
request/velocity values, and stale generations. Both optimized ARM64 and
x86-64/Rosetta portable builds agree exactly; regeneration is byte-identical.
These are bounded finite cases, not a claim about arbitrary non-finite inputs,
complete ECS selection or full-tick sneaking equivalence.


## Entity-inside traversal and region readiness

The caller `0x140B58960` chooses ordinary AABBShape bounds unless the optional
InsideBlockComponent override flag at byte 48 is set. Expansion is read at byte
52. Native constructor `0x140B80B90`, reached by getter `0x140B6D380`, zeros the
entire 64-byte component, including override and expansion. Step uses ordinary
bounds and zero expansion; non-default producers remain unported.

The complete query `0x1410EE9B0` first rejects an absent callback, then subtracts
expansion and adds `0.001f` to minima, adds expansion and subtracts `0.001f` from
maxima, with separate float rounding. Its CVTTSS2SI/CMOVBE floor correction has
specific NaN and negative-overflow behavior; it differs from the world-source
floor helper and is ported separately. The resulting integer endpoints are
converted back to float for one region readiness call with its boolean argument
false. If ready, every X/Y/Z cell is acquired and passed to the visitor, with Z
fastest; the function returns true even when an inverted cell range visits none.
Step no longer stops scanning when the first web cell is found.

LocalConstBlockSource vtable `0x1467AF088` slot 14 targets `0x143E8D580`.
It computes the wrapped X/Z chunk-count product, visits X fastest, stops at the
first absent chunk, and optionally rejects chunk byte 5352 only when requested.
The entity-inside caller passes false, so that extra byte is ignored there.
A zero product returns true without reading chunks. World maps this readiness
service to its current chunk availability adapter; real chunk-source lifetime
and other world predicates remain separate.

804 complete query executions cover callback/readiness absence, float boundary
coordinates, expansion, inverted/thin bounds, large positions and rejected
exceptional-coordinate queries. 512 complete readiness executions cover chunk
order, missing chunks, byte-5352 gating, early termination and empty ranges.
Only world readiness/acquisition/visitor services are synthetic; arithmetic,
iteration and decisions execute native instructions. Both optimized ARM64 and
x86-64/Rosetta ports match all fields. Fixtures regenerate byte-identically.
A Step regression verifies that an unavailable region does not accumulate web
slowdown. These tests do not establish arbitrary block-specific contact behavior.

A separate direct execution of the sprint-jump block at `0x140EAFD9C` with yaw
270 and zero initial horizontal velocity returns X `0x3E4CCCCC` and Z
`0xB7A081B4`. The legacy effects scenario expects zero Z at that heading. This
confirms a specific reference incompatibility with BDS. The later legacy-trace
migration below resolves that compatibility gate using a separate simulator
and executed BDS impulses, without Perlin-generated expectations. It does not
turn the rest of the legacy simulator into a whole-native BDS oracle.


## CI platform findings

The GitHub jobs for head `61ac44f` (run `39937551824`) independently report the
same 152 legacy trace discrepancies on Linux x86-64 and macOS ARM64; all other
52-target suite entries pass there. Windows compilation exposed a missing
standard `<bit>` include in BdsCollisionCache.hpp. The header now includes its
bit-cast dependency explicitly; local rebuild and the three affected cache
self-tests pass. A new Windows CI result is still required to verify that fix.
The later entity-inside additions have separate local ARM64/x86-64 evidence and
must also complete current-head CI before platform validation is claimed.


## Complete ground-jump callbacks

`BdsJump.hpp` ports the full ordinary callback `0x140EAFAF0` and alternate
callback `0x140EAFFD0`, beyond the previously tested sprint impulse. The
reference executes their original instructions and the native event constructor
`0x140EC4780`. Synthetic services supply world blocks, the optional height
callback, the synchronized-data type query, and mechanical ECS allocation/index
lookup. These services do not replace jump arithmetic or branch decisions.

The 1,536 cases check every queried block position, previous-jump position,
velocity, and the trigger's semantic bool/position fields. Native trigger padding
is uninitialized and is excluded. Cases include supplied zero/negative heights,
missing height providers, invalid/missing effect records, extreme signed effect
amplifiers, existing triggers, property-mask combinations, below-cell probing,
sprint headings, existing upward/downward velocity, and typed synchronized data.
Both optimized ARM64 and x86-64/Rosetta implementations match every checked bit;
regeneration is byte-identical. Runtime regressions check zero versus missing
height, previous position, and the lower-cell property multiplier.

Ordinary jumping copies position into MobJumpComponent, chooses forced `0.2f`
or optional height/default `0.42f`, adds `(float(amplifier)+1)*0.1f`, applies the
surface multiplier, and uses native max-with-existing vertical speed before the
sprint impulse. The alternate callback has distinct effect rounding
`float(amplifier)*0.1f+0.52f`, adds the resolved type-2 integer from
SynchedActorData slot 2 times `0.1f`, overwrites vertical speed without that max,
and leaves previous position and horizontal velocity unchanged. Both create
TriggerJumpRequestComponent only when absent, retaining an existing record.
BlockLegacy property bit 43 selects multiplier `0.6f`; bit 8 permits the lower
cell query when body-minimum rounding would otherwise skip it. Actual block
identities and property producers have not yet been established.

Native view constructors establish request identities:
`0x140EC27A0` selects OtherJumpRequestComponent `0x9D529862`;
`0x140EC2610` selects SquidJumpRequestComponent `0x3B31D80A` and forces the low
ordinary height; `0x140EC2420` selects LavaSlimeJumpRequestComponent `0x83B7F546`
and the alternate callback. The optional 88-byte component is JumpControlComponent (`0xBA17447D`):
its pool constructor `0x140E08B30` records the class name and type hash. The seven required ordinary view
members include AABBShape, ActorDataFlag, ActorRotation, MobEffects, MobJump,
StateVector, and the request. `0x140EB0720` dispatches ordinary, squid, lava-slime
and another predicate-gated ordinary path in that order; phase integration is
still under audit. Trigger cleanup helpers `0x140EC9800` and `0x140EC98D0` remove
one record or clear the pool, but their schedule and all consumers are not yet
reconstructed. Step therefore integrates ordinary calculations only and does
not pretend a persistent Perlin event is equivalent to native trigger lifetime.

Reproduce with the current ignored IDA export:

```sh
build/bds-oracle-venv/bin/python tools/bds_reference/jump.py \
  --export build/bds-reference.json --sine tests/data/bds_sine.bin \
  --out build/bds-jump-regenerated.txt
cmp build/bds-jump-regenerated.txt tests/data/bds_jump.txt
ctest --test-dir build -R '^(bds_jump_selftest|physics_selftest)$' --output-on-failure
```

Run `39938936441` on head `3c1959a` confirms Windows compiles and runs its suite:
47/48 Windows tests pass, with the same 152 legacy trace differences as macOS.
The missing-header compile failure is resolved. Local tests after the jump port
report 53/54 passing targets; the sole failing target and its 152 mismatches are
unchanged. No expectations or tolerances were changed.


## Jump request classification and cleanup ordering

`BdsJumpRequests.hpp` ports JumpFromGroundFilterSystem callback `0x140EAF890`.
Its wrapper `0x140EB4AB0` proves the tested optional memberships are
LocalPlayerComponent `0x63A37B09`, LavaSlimeFlagComponent `0x5F10CF3F`, and
SquidFlagComponent `0x881BF973`. Classification prioritizes local player, then
lava slime, then squid, with OtherJumpRequest as the fallback. It creates only
the selected request and only if that entity is absent from the destination.
Component payload bytes are not consulted. `BdsEntityMembership.hpp` preserves
18-bit sparse-page indexing, generation comparison, null/missing pages, and the
reserved packed-index rejection.

The 1,296 independent native executions cover the Cartesian product of six
membership layouts for all three classification views and the destination:
absent packed entry, valid entry, stale generation, out-of-range page, null page,
and reserved packed index. Inputs vary entity indices across page boundaries
and four generations. Tests compare the chosen pool query, insertion count and
inserted entity. Only pool acquisition and insertion services are synthetic;
all native membership and priority branches execute unchanged. Optimized ARM64
and x86-64/Rosetta ports match; fixtures regenerate byte-identically.

Registration `0x1402CBA10` orders JumpFromGroundFilterSystem, its
RemoveFromAllEntitiesSystem<JumpFromGroundRequestComponent>, JumpFromGroundSystem,
then cleanup of LocalPlayer/LavaSlime/Squid/Other jump requests. The latter
factory `0x140EB98A0` resolves through vtable `0x1466D1AF8` to `0x140EC9960` and
`0x140EC2130`, which clear all four destination pools. TriggerJumpRequest is a
separate component and is not included in that cleanup. ResetNoJumpDelaySystem's
view `0x140EC1BE0` requires MobJump, InterpolateMovementNeeded, and MobFlag while
excluding MobIsJumping and Passenger; its callback `0x140EB15A0` zeros MobJump's
integer at offset 16. Those facts establish part of the lifecycle but do not
prove the complete producer and consumer schedule.

The classifier is available as a portable ECS adapter with independent tests.
Perlin's player-only Step still uses its existing jump eligibility. Replacing
that eligibility before reconstructing request producers and the local-player
predicate would conflate classification with permission to jump. Therefore
these tests do not claim runtime entity scheduling, vehicle jump behavior, or
all request lifetimes are complete.

```sh
build/bds-oracle-venv/bin/python tools/bds_reference/jump_filter.py \
  --export build/bds-reference.json --out build/bds-jump-filter-regenerated.txt
cmp build/bds-jump-filter-regenerated.txt tests/data/bds_jump_requests.txt
ctest --test-dir build -R '^bds_jump_requests_selftest$' --output-on-failure
```

The full application build passes after this addition. Local CTest is 54/55;
the sole failure remains the same 152 legacy trace checks in four scenarios.


## Ground-jump permission predicate

The local-player callback `0x140EB0340` checks `0x140DEFBF0` before invoking
ordinary jump calculation. ActorDataFlag bits 69 (IN_SCAFFOLDING), 70
(OVER_SCAFFOLDING), and 71 (DESCEND_THROUGH_BLOCK) suppress jumping when either
scaffolding flag and descent are both set. The native enum table at
`0x146AFBD80` proves these identities. Step now applies this gate from its
existing GravityFlags. An integration regression covers suppression and descent
away from scaffolding; this does not prove the input producers of those flags.

The predicate separately rejects a powder-snow support block unless the actor
has CanStandOnSnowFlagComponent `0xA0CA7F35` or HasLightweightFamilyFlagComponent
`0x9EB7A38D`. The membership services resolve through vtable `0x1466C73F8` slots
5/6 to `0x140C6D940` / `0x140C6D8B0`. Native initializer `0x1400E3CB0` stores
`minecraft:powder_snow` into the hash/string object at `0x14715EAE0`; this
establishes the compared block identity without guessing from behavior.

The 2,048 reference cases execute permission branches in the original BDS
predicate with supplied supporting-block selection, block identity, and ECS
membership services. They exhaust the low eight bits of the upper actor-flag
word and both values of snow/standing/lightweight membership. Tests check the
permission result and lazy membership-query order: standing is queried only for
snow; lightweight only when standing was false. All checked outputs match in
optimized ARM64 and x86-64/Rosetta builds, and regeneration is byte-identical.

The synthetic support selector replaces `0x143262B80` for this predicate test.
It does not prove that selector or its world helper `0x143E943A0` is equivalent.
Those routines choose a support shape from an expanded query with world,
block-specific and actor collision services; their complete integration remains
unfinished. The portable full predicate is available, but Step currently
integrates only the proven scaffolding flag gate. Powder-snow selection,
standing/lightweight producers, leather-equipment transitions and the broader
local-player request schedule remain required work.

```sh
build/bds-oracle-venv/bin/python tools/bds_reference/jump_predicate.py \
  --export build/bds-reference.json --out build/bds-jump-predicate-regenerated.txt
cmp build/bds-jump-predicate-regenerated.txt tests/data/bds_jump_predicate.txt
ctest --test-dir build -R '^(bds_jump_predicate_selftest|physics_selftest)$' --output-on-failure
```

The full local application builds and CTest reports 55/56 passing targets.
The same 152 legacy trace differences remain; no fixture expectations,
exclusions or tolerances were changed during these jump additions.


## Honey identity, reduced jumps and contact movement

Native initializer `0x1400DEBE0` constructs `minecraft:honey_block` at
`0x1471572A8` (the name object's hash is at `0x1471572A0`). Registration at
`0x140584982` passes that name to factory `0x1406B78D0`. Its constructor block
sets vtable `0x1467C8540` and ORs BlockLegacy properties with `0x80000080000`,
which includes the reduced-jump bit 43. This confirms the surface identity.
Perlin now supplies that jump property for honey, including when collision boxes
come from a custom definition. Other sources of the jump-probe bit 8 remain
unmapped; the complete BlockLegacy property producer is not ported.

Entity-inside classification `0x140B55B90` recognizes honey's name hash
`0x2C975E4949D832F7` and accumulates ordered position/block records. Its movement
callback is `0x140B58CF0`, reached through view `0x140B64A70`. The view requires
honey contacts, AABBShape, StateVector and FallDistance, and excludes
WasInWaterFlagComponent `0x4086B5E6`. Each contact multiplies both horizontal
velocity lanes by `0.4f`, clamps vertical velocity to at least `-0.12f`, and may
clear fall distance when below the honey top and outside its side threshold.
The threshold is `bodyWidth*0.5f+0.43125f`; the top is block Y plus `0.9375f`.
Multiple contacts attenuate repeatedly, in their original order.

`BdsHoney.hpp` ports that complete callback. The 640 finite native executions
have no runtime hooks and verify all three velocity lanes and fall-distance
bits, with zero through four contacts, negative positions, boundary comparisons,
existing upward/downward/signed-zero speeds, thin/negative widths and repeated
contacts. Optimized ARM64 and x86-64/Rosetta ports match; regeneration is
byte-identical. Exceptional/non-finite input behavior is outside this fixture.

Step applies honey movement after its motion and before accumulated slowdown,
matching their relative order in `0x140B56930`. Runtime regressions cover side
fall reset, vertical clamp, water exclusion, and one versus two horizontal
attenuations. Honey's previous fallback speedFactor multiplier is removed to
avoid applying that attenuation twice. Name-based classification preserves the
honey identity independently of custom geometry selection.

WasInWaterFlag is produced by fluid-state processing, including `0x140E0E820`;
its name alone does not prove it means the prior tick's water boolean. The
current adapter uses Step's sensed water state. Native fluid classification,
passenger exclusions and exact producer timing remain under audit. Ordinary
player width supplies the dimension input; native custom actor-dimension
production, full generic-contact predicates, honey sound/particle events, and
all cross-mode/multi-tick integration still need porting.

```sh
build/bds-oracle-venv/bin/python tools/bds_reference/honey.py \
  --export build/bds-reference.json --out build/bds-honey-regenerated.txt
cmp build/bds-honey-regenerated.txt tests/data/bds_honey.txt
ctest --test-dir build -R '^(bds_honey_selftest|physics_selftest)$' --output-on-failure
```


## Resolution of the 152 legacy trace differences

Current validation: the full macOS ARM64 application build passes, all **58/58
CTest targets pass**, and `bedsim_trace_selftest` checks all **5,400/5,400 values**
without differences at its existing tolerances. No simulation implementation,
tolerance, scenario exclusion, or check count was changed to obtain this result.
The checker now also rejects non-finite values, malformed expectations and an
incomplete/empty fixture instead of accepting a reduced set of comparisons.

All 152 former failures were horizontal values in four 50-tick scenarios. The
legacy sprint formula converted yaw to radians, then multiplied by `10430.378f`
for a table lookup. BDS's original sprint block converts degrees directly with
`182.04443f`. Float rounding changes the selected table entry at headings such
as 45, 135 and 270 degrees. At yaw 270, the native cosine impulse is a small
negative value rather than the legacy zero; the resulting drift persists over
later ticks. Perlin's already ported calculation was correct for this block.

To correct the stale expectations independently, the migration uses
[`artemis-bedrock/bedsim-cpp` at
`6a18b0e95b184a9392d13bff9a69d713230a7135`](https://github.com/artemis-bedrock/bedsim-cpp/tree/6a18b0e95b184a9392d13bff9a69d713230a7135).
The original generator for Perlin's old fixture was not retained; this pinned
implementation independently reproduces all 1,800 checked values in the four
affected scenarios within their original tolerance. This establishes a
compatible baseline, not undocumented original-generator provenance.

`tools/bds_reference/legacy_trace_replay.cpp` adapts only those scenarios to
that simulator and includes no Perlin physics implementation. The migration:

1. Verifies the clean, pinned independent source and the SHA-256 of the original
   four-scenario snapshot `tests/data/bedsim_legacy_sprint_traces.txt`.
2. Compiles the unmodified simulator and requires all 1,800 original checks to
   pass. Unsupported scenario facts or missing frames are rejected.
3. Builds a second copy with only its sprint-impulse calculation delegated to
   the Python parent. For all seven accepted sprint jumps, the parent executes
   unmodified BDS instructions `[0x140EAFD9C, 0x140EAFDFA)` with the BDS sine
   table, Windows register convention and MXCSR `0x1F80`. No runtime calls or
   arithmetic instructions inside that block are replaced.
4. Requires this independently corrected replay to reproduce the same 152
   disagreements with the original snapshot. It rejects any change to vertical
   motion or collision decisions between the two independent runs.
5. Migrates only horizontal expectation fields changed by those impulses.
   All scenario inputs, all vertical expectations, all collision/ground flags
   and every other scenario remain intact. This updates 390 horizontal fields
   in 155 records, including changes smaller than the failure tolerance.

The migration records seven native input/index/output tuples in
`tests/data/bedsim_sprint_jumps.txt`. `bds_sprint_trace_selftest` checks both
lookup indices and both output velocity lanes bit-for-bit on optimized ARM64
and x86-64/Rosetta. These seven executions count toward the isolated native
reference denominator. The 200 independently replayed compatibility frames do
**not** count as native BDS simulation outputs. Their remaining calculations
come from the pinned external compatibility simulator; complete native
scheduler/world scenarios remain a separate requirement for overall parity.

On the validated macOS ARM64 compiler, both the corrected fixture and native
impulse transcript regenerate byte-identically, including when the tool is rerun
against the corrected file. Other compatibility-simulator toolchains may differ
in their remaining host-libm calculations; ordinary regression checks retain the
existing tolerance. The seven native impulse comparisons themselves are exact.
The original snapshot is preserved in the repository for audit and reproduction.
Reproduce using a separate clean checkout under ignored build paths:

```sh
git clone https://github.com/artemis-bedrock/bedsim-cpp.git build/bedsim-cpp-reference
git -C build/bedsim-cpp-reference checkout 6a18b0e95b184a9392d13bff9a69d713230a7135
build/bds-oracle-venv/bin/python tools/bds_reference/migrate_legacy_sprint.py \
  --reference-dir build/bedsim-cpp-reference --export build/bds-reference.json \
  --sine tests/data/bds_sine.bin --legacy tests/data/bedsim_legacy_sprint_traces.txt \
  --fixture tests/data/bedsim_traces.txt --out build/bedsim-traces-migrated.txt \
  --jump-out build/bedsim-sprint-jumps.txt
cmp build/bedsim-traces-migrated.txt tests/data/bedsim_traces.txt
cmp build/bedsim-sprint-jumps.txt tests/data/bedsim_sprint_jumps.txt
ctest --test-dir build --output-on-failure
```

The migration needs a C++23 compiler and the existing pinned Unicorn environment.
Ordinary CTest runs use committed fixtures and require neither an external
simulator checkout nor BDS executable pages. Those pages remain untracked.
Earlier failing-gate results elsewhere in this document describe the historical
state before this migration; the current gate is green locally.


## Native fluid-state and common liquid-collector port

The reference remains the same BDS binary and SHA-256 above. Registration
`0x1402D15D0` names LiquidBlocksFetch, InLavaSensingSystem,
InWaterSensingSystem, UnderWaterSensingSystem and LiquidSplashRequestSystem.
The first three lead to callbacks `0x140FC98E0`, `0x140E0E6F0` and
`0x140E0E820`. Registration order alone does not establish all native execution
or deferred component-mutation timing.

`BdsFluidState.hpp` ports these verified behaviors:

- Water and lava contract the supplied body using native float arithmetic,
  respectively by `(0.001, 0.401, 0.001)` and `(0.1, 0.4, 0.1)`. Crossed
  axes collapse to their original center. The common readiness query uses the
  union of both contracted boxes. An optional native membership bypasses
  contraction; its identity/producer is still unresolved, so ordinary player
  integration uses contraction.
- Both fluid regions are tested, with lava winning material selection when both
  intersect. This fixes the previous water-first adapter. The native request's
  separate water-intersection byte can still be true when selected material is
  lava; it is not interchangeable with WasInWaterFlag.
- The common liquid collector computes lower floors and `floor(upper + 1.0f)`,
  traverses Y/Z/X with X fastest, emits matching-material records and their four
  horizontal boundary-face bits, and supports material 25 as a wildcard. Without
  a destination vector it returns at the first admitted cell. With a destination
  it keeps collecting after a hit. The hit predicate checks integer cell origins
  against floored lower/original upper bounds. Fluid surface height is absent
  from this body-sensing path; head sensing is separate. This fixes the old
  body's comparison with `fluidLevel`.
- InLavaSensing sets membership exactly for selected material 6. InWaterSensing
  removes membership for non-water material or a valid boat passenger. Passenger
  sparse membership, referenced-registry ID and boat-view membership are distinct
  gates. Otherwise it resets fall distance unless FallingBlockFlag is present.
  When WasInWaterFlag is absent, it creates that flag and get-or-creates a splash
  request followed by a dword store of 1. Continued water membership preserves an
  existing splash payload, including values other than 1.

Ordinary Step uses these body bounds, material selection, collector and
water/lava flag logic. It has no Passenger/FallingBlock component, so those
runtime predicates are false. A pending splash payload is retained in State;
its subsequent replacement, erasure, event and presentation consumers are not
implemented. Integration regressions cover water entry/stay/exit/reentry,
fall-distance reset, pending-payload retention, adjacent water/lava cells and
body contact above a shallow rendered liquid surface. These integration checks
reuse the behavioral findings and are not additional native-reference cases.

The independent fixtures contain **4,256 native executions**:

| Reference output | Executions |
| --- | ---: |
| Complete InWaterSensing callback, supplied optional sparse pools | 2,048 |
| Complete InLavaSensing callback | 128 |
| Chained water callbacks, 32 actors with 16 updates each | 512 |
| Complete LiquidBlocksFetch, selected material/intersection outputs | 32 |
| Complete LiquidBlocksFetch, finite water/lava/readiness bounds | 1,024 |
| Complete common liquid collector, bounds conversion and record writes | 512 |

Both new portable suites match every checked bit, membership, queried cell,
record position and boundary mask on optimized ARM64 and x86-64/Rosetta builds.
The collector fixture includes narrow/zero extents, negative coordinates,
rounding boundaries and large coordinates up to the native world range. The
chained water fixtures preserve each actor's preceding native flag, fall-distance
and pending splash output, with no synthetic splash consumption between updates.
They do not execute a complete native multi-system tick.

`fluid_state.py` substitutes ECS pool getters, insertion/erasure, the composite
boat-view query, block collection and world readiness as documented. The native
water callback still executes its own sparse membership, referenced registry-ID
checks and all branch decisions. The finite-bounds/selection cases execute the
complete fetch callback with flow disabled and a finite-only fdclass service.
Readiness returns true. `liquid_cells.py` separately executes the real common
collector and bounds conversion, supplying only deterministic block reads. It
preallocates record capacity, uses no secondary bodies and does not exercise
allocator growth. No fluid-surface-height or flow decision is substituted into
the real collector. All original callback code and function hashes come from the
read-only IDA export; executable pages are not committed.

Reproduce after exporting the added functions with `export_ida.py`:

```sh
build/bds-oracle-venv/bin/python tools/bds_reference/fluid_state.py \
  --export build/bds-reference.json --out build/bds-fluid-state-regenerated.txt
cmp build/bds-fluid-state-regenerated.txt tests/data/bds_fluid_state.txt
build/bds-oracle-venv/bin/python tools/bds_reference/liquid_cells.py \
  --export build/bds-reference.json --out build/bds-liquid-cells-regenerated.txt
cmp build/bds-liquid-cells-regenerated.txt tests/data/bds_liquid_cells.txt
ctest --test-dir build --output-on-failure
```

Both fixtures regenerate byte-identically. The full local application build and
**60/60 tests pass**, including all **5,400 legacy trace checks** without changing
those fixtures or tolerances. All seven CI build jobs, including Windows, passed
on the preceding commit `17a5957` (run 39944748400); new-head CI is reported in the
PR as it completes.

The new total is **127,465 exact native reference cases/entries**, including the
512 chained water callbacks above and excluding the separately counted 288 glide
ticks and all external compatibility-simulator frames. This remains a bounded
reference count, not an overall BDS physics accuracy percentage.

Fluid work still required includes real world readiness and multiple block
layers, secondary bodies, flow admission/aggregation, the underlying flow-vector
producer, head-height/surface sensing, swimming/lava/bubble movement producers,
passengers, full native system ordering and all request consumers/cleanup.
The later LiquidSplashRequest callback `0x140FCA240` has been located: it considers
ActorGameType, world game type, head-water transitions and WasInLava, and can
replace or erase the pending splash request and create PostSplashGameEventRequest.
That callback and its lifetime interactions are not yet ported. These explicit
remaining paths prevent a claim of complete 1:1 physics.

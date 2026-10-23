Player animation and body rotation use the Minecraft Java 26.3 client as a
behavioral reference. The reference JAR has SHA-1
`e877b6a07acd633fb3bb475002175cec036e7b87`; it is not bundled with Perlin.
The relevant classes are `LivingEntity`, `WalkAnimationState`,
`BodyRotationControl`, `HumanoidModel`, and `AvatarRenderer`.

`JavaAnimation.hpp/.cpp` implement the player torso movement direction, backward
walking, 50-degree head/body limit, attack turn, walking phase and speed, idle
arm bob, crouching, riding, attack shoulder offsets, swimming cycle, and common
held-item arm poses. Mobs with head/body geometry use the separate mob rotation
controller (75-degree head limit, ten-tick idle delay and ten-tick realignment).
Their species-specific animation controllers remain resource driven.

Motion is sampled at 20 Hz along the interpolated frame trajectory. Rendering
interpolates the last two simulation poses. Catch-up frames sample each crossed
tick rather than applying the entire frame's movement once; teleports reset
walking, torso interpolation and cape state. Velocity queries use elapsed time. World, first-person and inventory doll views
maintain separate animation state.
Riding, sleeping and death stop the walking cycle.

Java's player model parts are independent. Bedrock arms and heads often inherit
the torso transform, and armor can have a different hierarchy again. The pose
adapter fits each part's world transform to the actual skin or armor skeleton.
Sleeves, skin layers and held-item locators inherit the fitted bones. Native
player poses supersede the old additive player locomotion/crouch overrides;
first-person animations and non-player animation controllers keep their existing
resource animation paths. Custom geometries without the six humanoid parts
retain their resource animations. `PERLIN_NO_JAVA_ANIMATIONS=1` selects the
resource-driven player pose for comparison.

Validation:

```
cmake --build build --target Perlin entity_animation_selftest
ctest --test-dir build -R '^(entity_animation_selftest|attachable_selftest)$' --output-on-failure
```

The animation self-test checks 10, 15, 20, 30, 60, 144 and 240 FPS trajectories,
angle wrapping, idle and backward movement, mob idle alignment, walk damping,
riding and attack angles, swimming phases, and actual bone world matrices with
compound crouch/attack/swim poses and attached sleeves.

Offline visual checks can use `PERLIN_PREVIEW_ENTITIES=player:cape,player:alex`,
`PERLIN_PREVIEW_ENTITY_WALK=1`, `PERLIN_PREVIEW_TURN=1`,
`PERLIN_PREVIEW_SNEAK=1`, `PERLIN_PREVIEW_SWIM=1`,
`PERLIN_PREVIEW_ARMOR=diamond` or `PERLIN_PREVIEW_ATTACK=0.3` with the usual
`PERLIN_PREVIEW=1`, `PERLIN_PREVIEW_SHOT` and `--offline` flags.

Bedrock does not expose every Java animation input. Offhand attack identity,
charged crossbow item components and the newer spear swing descriptions are
not yet represented in Perlin's entity state. Those cannot be reproduced
exactly from head/body rotations and the current generic swing event alone.

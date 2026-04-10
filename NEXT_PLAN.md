# Project Raceman Next Plan

## Current Status

Recent physics/editor work completed:

- Rigidbody body types: `Static`, `Kinematic`, `Dynamic`
- Rigidbody settings: mass, gravity, linear damping, angular damping
- Rigidbody velocities: linear and angular
- Rigidbody axis locks:
  - Freeze Position: X / Y / Z
  - Freeze Rotation: X / Y / Z
- Scene save/load support for the new rigidbody fields
- Inspector support for single and multi-selection editing

## Priority Order

1. Rigidbody completion pass
2. Collider expansion
3. Physics materials and filtering
4. Scripting API expansion
5. General mesh import improvements
6. Vehicle/editor integration

## 1. Rigidbody Completion Pass

Goal: make rigidbodies usable for actual gameplay objects, not just basic simulation.

Planned work:

- Add friction and restitution to rigidbody/component data
- Add sleep controls:
  - allow sleeping
  - wake body from script/editor when values change
- Add collision quality / CCD option for fast bodies
- Add kinematic motion workflow:
  - move kinematic body by target transform instead of just raw velocity
- Add force/impulse APIs:
  - add force
  - add impulse
  - add torque
  - add angular impulse
- Expose runtime body state to scripts:
  - angular velocity getters/setters
  - force/impulse helpers
  - wake/sleep helpers

Done when:

- Common cases like moving platforms, pickups, pushable props, and thrown objects can be authored without engine-side hacks.

## 2. Collider Expansion

Goal: support real level/world collision authoring.

Planned work:

- Add `MeshCollider` for static environment geometry
- Add `ConvexHullCollider` for dynamic objects
- Add compound collider authoring improvements
- Add editor visualization for collider shapes and centers
- Add auto-fit helpers from mesh bounds:
  - fit box collider from mesh
  - fit sphere collider from mesh
  - fit capsule collider from mesh

Done when:

- Track geometry, walls, ramps, and large props can use proper collision without manual primitive-only approximations.

## 3. Physics Materials And Filtering

Goal: make collisions tunable and predictable.

Planned work:

- Add per-body or per-collider:
  - friction
  - restitution
- Add collision layers / masks
- Add trigger filtering rules in editor/runtime
- Add named presets for common setups:
  - player
  - world
  - prop
  - trigger
  - vehicle

Done when:

- Designers can control what collides with what, and how it behaves, directly in scene data.

## 4. Scripting API Expansion

Goal: let scripts drive physics and character gameplay cleanly.

Planned work:

- Expand `ObjectScriptContext` rigidbody functions:
  - get/set angular velocity
  - add force
  - add impulse
  - add torque
  - add angular impulse
- Add transform/physics convenience helpers:
  - teleport rigidbody
  - wake rigidbody
  - query grounded-like trigger overlaps later if needed
- Add example scripts for:
  - moving platform
  - rotating hazard
  - pushable crate
  - projectile test

Done when:

- Gameplay experiments no longer require engine changes for basic physics interactions.

## 5. General Mesh Import Improvements

Goal: stop treating project mesh assets as OBJ-only in the editor.

Planned work:

- Generalize project mesh picker to support:
  - `obj`
  - `gltf`
  - `glb`
  - `fbx`
- Keep Assimp import path centralized
- Improve imported material/texture mapping
- Add better error logging for unsupported/broken mesh assets
- Add mesh asset metadata cache if import cost becomes noticeable

Done when:

- Imported mesh workflow is asset-type agnostic from the editor point of view.

## 6. Vehicle And Editor Integration

Goal: connect the custom vehicle stack to authored scene objects.

Planned work:

- Define a vehicle component/data model in scenes
- Bind wheel transforms to authored mesh objects
- Add vehicle debug inspector:
  - speed
  - gear
  - wheel slip
  - suspension travel
- Add spawn/reset flow for vehicle testing in play mode
- Add test scene specifically for car handling iteration

Done when:

- Vehicles can be authored, tuned, and tested through the editor instead of custom code only.

## Suggested Immediate Next Sprint

If continuing from the rigidbody work just completed, do this next:

1. Add rigidbody `friction` and `restitution`
2. Add rigidbody script API for force/impulse/torque
3. Add `MeshCollider` for static world geometry
4. Add generalized mesh picker support for `glb` and `gltf`

## Notes

- Keep scene serialization backward-compatible where possible
- Prefer engine-level features that improve editor usability and scriptability together
- Avoid adding many collider primitives before `MeshCollider` and `ConvexHullCollider`
- Prefer `glTF/GLB` as the first non-OBJ import target in editor UX

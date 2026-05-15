# Dynamic Shader Properties For Materials

## Summary
Add a generic material property system so shaders can define values that appear in the Material Inspector. Use it for the emissive shader first, then make it usable by shader graphs later.

## Key Changes
- Extend `Material` with dynamic saved properties, separate from the current fixed fields:
  - supported types: `float`, `vec2`, `vec3/color`, `vec4/color`, `bool`, `texture2D`
  - save/load them under a new JSON `"properties"` section
  - keep old fields working for existing materials

- Extend `ShaderRegistry` so each shader can declare inspector properties:
  - PBR keeps current fields: albedo, metallic, roughness, emissive, texture slots
  - Emissive gets proper fields:
    - `Albedo Color`
    - `Albedo Texture`
    - `Emissive Color`
    - `Emissive Intensity`
    - optional `Emissive Texture`
    - `UV Tiling`
    - `UV Offset`
  - Inspector renders fields from the shader definition instead of only fixed capability booleans

- Update emissive shader/runtime binding:
  - add uniforms like `uEmissiveIntensity`, `uEmissiveTexture`, `uUseEmissiveTexture`
  - final color becomes base color plus emissive color multiplied by intensity and optional emissive texture
  - renderer binds dynamic material properties before drawing

- Prepare shader graph compatibility:
  - graph shaders still support the current fixed uniforms
  - add the property system in a way that later graph nodes can be marked “Expose to Material”
  - do not require full graph-exposed parameters in this first pass unless needed immediately

## Inspector Behavior
- Changing shader updates visible material fields.
- Existing materials still load and show the old values.
- New emissive materials show emissive-specific controls instead of unrelated metallic/roughness fields.
- Texture fields remain browsable/clearable like current fixed texture paths.
- Material undo/redo captures dynamic property edits too.

## Test Plan
- Load old `.mat.json` files and confirm no values are lost.
- Create/select an emissive material and verify `Emissive Intensity` affects Scene/Game view.
- Save/reload material and confirm dynamic values persist.
- Switch between PBR and Emissive and confirm only relevant fields appear.
- Confirm PBR rendering and shader graph rendering still work with existing materials.

## Assumptions
- Use the generic system, not an emissive-only patch.
- Keep current fixed material fields for backward compatibility.
- First implementation exposes built-in shader properties; full shader graph “Expose to Inspector” nodes can be added after this foundation.

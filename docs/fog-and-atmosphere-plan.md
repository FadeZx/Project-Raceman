# Fog & Atmosphere — Implementation Plan

> **Status:** Phases 0–3 are implemented and running in the editor.
> Phase 2 was folded into Phase 1: the sun-inscatter lobe costs one extra uniform
> and one dot product, and doing it separately would have meant editing all six
> material shaders twice. Phases 3–5 remain open.
>
> Deviations from the plan as written below:
> - Only `pbr.fs` needed the `AmbientBuffer` transmittance fix; the other material
>   shaders already write a zero ambient term, so the SSAO subtraction is a no-op
>   for them.
> - Sky fog is restricted to `ExponentialHeight`. Linear fog is a start/end depth
>   ramp and the sky sits past any end distance, so applying it would always flatten
>   the whole skybox to fog colour.
> - `unlit.fs` does receive fog (see the note in that file for why).
> - `Renderer::GetFogUniforms()` caches the last known sun direction, because the
>   sky is drawn before `SubmitDraws` has populated the light list.
> - Phase 3 samples the prefiltered cubemap on **texture unit 11**, aliasing the
>   unit `uPrefilterMap` already uses, because units 0–15 were fully allocated and
>   GL only guarantees 16. It therefore requires a baked environment and falls
>   back to `fogColor` without one — that constraint is what keeps the two
>   samplers from ever disagreeing about what is bound to unit 11.

Target: exponential height fog + aerial perspective + directional (sun) inscattering,
with volumetric light shafts as a later phase.

Rationale: this is the highest visual-impact-per-hour item in the renderer gap list.
A racing game reads its sense of speed and scale off the horizon, and right now the
engine has no distance attenuation of any kind (`grep fog` returns zero hits).

---

## 1. Core design decision: in-shader, not post-process

Two ways to do this:

| | Post-process pass | In-shader (chosen) |
|---|---|---|
| Where | New fullscreen pass in `ResolveViewportTarget`, reads depth | Applied at the end of each material `.fs` |
| Transparency | **Broken** — blended geometry doesn't write depth, so glass, tyre spray and windows never fog | Correct for free |
| Skybox horizon haze | Needs a special case (sky has no depth) | Natural — sky shader fogs against a synthetic far distance |
| Cost | Extra fullscreen pass + bandwidth | ~15 ALU in an already-heavy shader, no extra bandwidth |
| Cost when disabled | Pass skipped | Branch on a uniform |

The engine is a **forward** renderer, which makes the in-shader path strictly better:
we already have `vWorldPosition` and `uCameraPosition` in every material shader, and
transparent surfaces go through the same lighting shader as opaque ones.

The one thing that genuinely wants a screen-space pass is **volumetric light shafts**
(Phase 4). Those are separate and additive.

### Consequence: the shader loader needs `#include`

`Shader::Shader()` in [`src/rendering/shader.h:20`](../ProjectRaceman/src/rendering/shader.h)
does a raw `ifstream` read with no preprocessing. Applying fog in-shader means the fog
function has to exist in `pbr.fs`, `transparent.fs`, `emissive.fs`, `unlit.fs`,
`vertex_color.fs`, `skybox.fs`, plus every graph-generated and user-authored `.fs`.

Copy-pasting it into eight files is not an option. **Phase 0 adds `#include` support**,
which is a ~40-line change in one place and is a hard prerequisite for almost everything
else on the renderer roadmap (clustered lighting, layered car paint, shared BRDF code).

---

## 2. Where the settings live

### The split that matters

Fog has two kinds of knobs, and they belong in different places:

- **Appearance** (colour, density, falloff, height, sun inscatter) — this is *authored art*.
  It is part of the look of a track. It must **never** be touched by the quality preset:
  switching from Ultra to Low should not change what the weather looks like.
- **Quality** (volumetric on/off, froxel resolution, light-shaft sample count) — this is a
  *performance* knob and belongs in the quality tiers.

### Concrete recommendation

Put both in `GraphicsProfile` ([`Renderer.h:74`](../ProjectRaceman/src/rendering/Renderer.h)),
but **exclude the appearance fields from `ApplyGraphicsPreset`**, exactly the way
`ambientColor` is already excluded today ([`MenuController.cpp:32`](../ProjectRaceman/src/ui/MenuController.cpp)).

This is the pragmatic call: persistence is a single flat `"graphics"` JSON block
([`SceneEditorPersistence.cpp:3176`](../ProjectRaceman/src/ui/SceneEditorPersistence.cpp) load,
`:3640` save) driving one `graphicsProfile_` member, and introducing a parallel
`EnvironmentSettings` struct means a second block, a second member, second getter/setter
and second wiring path — ~40 lines of boilerplate for two features' worth of data.

**Trigger for the refactor:** when Phase 5 (physical sky / time-of-day / clouds) lands.
At that point environment data outweighs the boilerplate and `EnvironmentSettings` should
be extracted, taking `ambientColor`, the skybox reference and all fog fields with it.

**Known limitation to accept for now:** the graphics profile is per *project*, not per
*scene*. Different tracks can't have different fog until the extraction happens. Note it
and move on — a single tuned fog setup is enough to validate the feature.

### Fields to add to `GraphicsProfile`

```cpp
// --- Environment: fog ---------------------------------------------------
// Authored look, not performance. Deliberately NOT written by
// ApplyGraphicsPreset: quality tiers must not change what the weather is.
enum class FogMode { Off, Linear, ExponentialHeight };

FogMode fogMode{FogMode::Off};
glm::vec3 fogColor{0.62f, 0.68f, 0.76f};
float fogDensity{0.015f};        // extinction at fogBaseHeight, per metre
float fogHeightFalloff{0.05f};   // 0 = uniform fog, higher = thinner with altitude
float fogBaseHeight{0.0f};       // world Y where fogDensity applies
float fogStartDistance{5.0f};    // metres of clear air before fog begins
float fogMaxOpacity{1.0f};       // caps transmittance; <1 keeps distant shapes readable
bool  fogAffectsSky{true};       // horizon haze band — the main scale cue
bool  fogUseSkyColor{false};     // sample prefiltered env instead of fogColor
// Directional (sun) inscattering
glm::vec3 fogSunColor{1.0f, 0.85f, 0.6f};
float fogSunIntensity{0.0f};     // 0 disables the second lobe entirely
float fogSunExponent{8.0f};      // tightness of the glow around the sun
// Linear mode only
float fogLinearStart{20.0f};
float fogLinearEnd{300.0f};

// --- Quality (DOES belong in ApplyGraphicsPreset) -----------------------
bool volumetricFog{false};       // Phase 4
int  volumetricFogQuality{1};    // 0 low / 1 medium / 2 high
bool fogDebugView{false};        // Scene View only, cleared by ResolveProfileForTarget
```

---

## 3. The fog model

Exponential height fog with analytic integration along the view ray — the UE4 model.
Cheap, closed-form, no marching.

Density at height `y`: `d(y) = fogDensity * exp(-fogHeightFalloff * (y - fogBaseHeight))`

Integrated optical depth from camera `C` to world point `P`:

```glsl
float FogOpticalDepth(vec3 cameraPos, vec3 worldPos) {
    vec3  delta      = worldPos - cameraPos;
    float rayLength  = length(delta);
    rayLength = max(rayLength - uFogStartDistance, 0.0);
    if (rayLength <= 0.0) return 0.0;

    float baseDensity = uFogDensity *
        exp(-uFogHeightFalloff * (cameraPos.y - uFogBaseHeight));

    // Analytic integral of exp(-falloff * y) along the ray. The t -> 0 limit is 1.0
    // (horizontal ray, constant density); guard it or horizontal views blow up.
    float t = uFogHeightFalloff * delta.y;
    float integral = abs(t) > 1e-4 ? (1.0 - exp(-t)) / t : 1.0;

    return baseDensity * rayLength * integral;
}
```

Transmittance and application:

```glsl
vec3 ApplyFog(vec3 color, vec3 worldPos, vec3 viewDir) {
    float opticalDepth  = FogOpticalDepth(uCameraPosition, worldPos);
    float transmittance = exp(-opticalDepth);
    transmittance = mix(1.0, transmittance, uFogMaxOpacity);

    vec3 inscatter = uFogColor;
    if (uFogSunIntensity > 0.0) {
        // Second lobe: fog lit by the sun, tight around the sun direction.
        // This is what produces haze glow on a low sun — the single biggest
        // "expensive engine" tell in a racing screenshot.
        float sunAmount = pow(max(dot(-viewDir, uSunDirection), 0.0), uFogSunExponent);
        inscatter = mix(uFogColor, uFogSunColor, sunAmount * uFogSunIntensity);
    }
    return color * transmittance + inscatter * (1.0 - transmittance);
}
```

`uSunDirection` comes from the directional light already resolved for shadows
(`shadowLightIndex`, [`Renderer.cpp:2295`](../ProjectRaceman/src/rendering/Renderer.cpp)).
Feed `vec3(0)` and intensity 0 when there is no directional light.

**`fogUseSkyColor` variant (Phase 3):** replace `uFogColor` with
`textureLod(uPrefilterMap, viewDir, 4.0).rgb`. Fog colour then tracks the sky
automatically — sunset fog goes orange with no artist input. Requires `uEnableIbl`;
fall back to `uFogColor` when the environment isn't baked.

---

## 4. Traps

These will each cost an afternoon if hit blind.

### 4.1 SSAO composite double-darkening — the important one

[`ssao_composite.fs:26`](../ProjectRaceman/src/shaders/post/ssao_composite.fs) does:

```glsl
FragColor = vec4(max(scene.rgb - ambient * occlusionStrength, vec3(0.0)), scene.a);
```

It **subtracts** the un-fogged `AmbientBuffer` from the scene colour. If fog is baked
into `FragColor` but not into `AmbientBuffer`, distant fogged pixels get the full
un-attenuated ambient subtracted from a washed-out colour → crushed black patches in fog.

**Fix:** attenuate the ambient MRT write by the same transmittance in
[`pbr.fs:428`](../ProjectRaceman/src/shaders/default/pbr.fs):

```glsl
AmbientBuffer = vec4(ambient * transmittance, albedoSample.a);
```

Compute `transmittance` once, use it for both writes. Do the same in every material
shader that writes `AmbientBuffer`.

### 4.2 Skybox

[`skybox.fs`](../ProjectRaceman/src/shaders/skybox/skybox.fs) is `#version 330` while
`pbr.fs` is `450` — the include has to compile under both, so keep the fog helpers to
plain GLSL (no `textureLod` on cube arrays, no bit ops).

The sky has no world position. Synthesise one: `cameraPos + normalize(TexCoords) * 1e5`,
then run the same `ApplyFog`. Looking at the horizon, `delta.y ≈ 0` → the `t → 0` guard
carries it and you get a dense haze band. Looking up, `delta.y` is large and positive →
the integral collapses toward zero and the zenith stays clear. That falls out of the
formula for free and is exactly the behaviour you want.

Gate it on `fogAffectsSky`. Skybox also writes `NormalBuffer = vec4(0.0)` (alpha 0),
which the SSAO composite early-outs on — leave that alone.

### 4.3 Exposure and tonemapping

Fog is applied in **linear HDR**, before tonemap. If `fogColor` is authored as an sRGB
swatch in the colour picker, it must be converted to linear before upload or fog will
read washed-out and desaturated. Check what `ImGui::ColorEdit3` gives you for
`ambientColor` today and match that convention.

Also: dense fog raises average scene luminance a lot. With a fixed exposure this reads
as "everything got brighter." This is the argument for auto-exposure being the natural
next item after fog — the two are coupled.

### 4.4 SSR and TAA

- **SSR** ([`Renderer.cpp:1624`](../ProjectRaceman/src/rendering/Renderer.cpp)) runs on
  the composite texture, which will already be fogged. Reflections then sample fogged
  scene colour — approximately right (the reflected ray really does travel through fog),
  slightly over-fogged for near reflections. Acceptable; do not try to fix it in Phase 1.
- **TAA** is unaffected — fog is a smooth function of world position and reprojects cleanly.
- **Motion blur** unaffected.

### 4.5 Reflection probe bakes

`BakeReflectionProbe` / `UpdateRealtimeReflectionProbe` render the scene through the same
shaders. Fog will be baked into probe cubemaps. That is *correct* for a static fog setup
and wrong if fog is later animated. Note it; revisit if fog becomes dynamic.

---

## 5. Phasing

### Phase 0 — `#include` support in the shader loader
**Files:** `src/rendering/shader.h`

Add a `ResolveIncludes(source, directory, depth)` static that scans for
`#include "path"`, splices the file contents inline, tracks a depth limit (say 8) and a
visited set for cycles. Run it on both vertex and fragment source before
`glShaderSource`.

Two things to get right:
- **Error line numbers.** Splicing shifts every line after the include, so GLSL error
  logs point at the wrong line. Emit `#line 1 <n>` directives around spliced content, or
  at minimum note the offset in `compileLog_`. This matters because
  `TryCompileMaterialShader` surfaces those logs in the editor.
- **Hot reload.** `InvalidateMaterialShader` keys off the shader id; editing an included
  `.glsl` won't invalidate anything. Simplest fix for now: make the "Recompile All
  Shaders" path (`InvalidateAllMaterialShaders`) the documented way to pick up include
  edits. Proper dependency tracking can wait.

**Verify:** move an existing helper (e.g. `FresnelSchlick`) into
`src/shaders/common/brdf.glsl`, include it from `pbr.fs`, confirm the viewport is
pixel-identical.

### Phase 1 — Height fog, constant colour
**Files:** new `src/shaders/common/fog.glsl`; `pbr.fs`, `transparent.fs`, `emissive.fs`,
`vertex_color.fs`, `skybox.fs`; `Renderer.h` (profile fields); `Renderer.cpp` (uniform
upload alongside the existing per-draw block at ~`:2719`); `MenuController.cpp` (UI);
`SceneEditorPersistence.cpp` (load/save).

Ship `FogMode::ExponentialHeight` + `Linear`. No sun lobe yet, no sky-colour sampling.
Remember the `AmbientBuffer` fix from 4.1.

**Verify:** a long straight with barriers receding to the horizon. Sanity check that
sliding density to 0 is pixel-identical to `FogMode::Off`.

### Phase 2 — Directional inscattering
Adds `uSunDirection`, `fogSunColor`, `fogSunIntensity`, `fogSunExponent`. Needs the
resolved directional light index plumbed into the material uniform block.

**Verify:** low sun, camera panning through it — glow should tighten as the exponent rises.

### Phase 3 — Sky-matched fog colour
`fogUseSkyColor` sampling `uPrefilterMap`. Cheap, and it removes most of the fog-colour
authoring burden.

### Phase 4 — Volumetric light shafts
Now a genuine screen-space pass, slotting into `ResolveViewportTarget` between SSR and
DOF. Radial blur from the sun's screen position against a depth-derived occlusion mask,
or froxel volumetrics if you want shadowed shafts. This is where `volumetricFog` and
`volumetricFogQuality` earn their place in the quality tiers.

New `ViewportTarget` members (`lightShaftFramebuffer`, `lightShaftTexture`) following the
existing allocation/destroy pattern, at half resolution.

### Phase 5 — Physical sky + time of day
Out of scope here. This is the point where `EnvironmentSettings` gets extracted.

---

## 6. UX — where the settings go

### Current layout of Project Settings → Rendering

```
Ambient Light                          <- environment, sits above the profile already
─────────────────────────
Graphics Profile
  Render Style / Quality Tier / Anti-Aliasing / TAA params
  Exposure / Output Mode / HDR / Bloom
SeparatorText("Post Processing")
  Motion Blur / DoF / Color Grading / Vignette / Film Grain / SSAO
  Shadows... / Reflections... / Weather... / Particles
  LOD / Dynamic Resolution / VSync / Culling / Draw Call Sorting
CollapsingHeader("Skybox")
```

Note that `Ambient Light` is already rendered *above* the `Graphics Profile` separator,
with a tooltip explaining it's project-wide. The file already makes the
environment-vs-profile distinction in the UI even though both live in the same struct.
Lean into that.

### Proposed

Insert an **Environment** block in that existing pre-profile region:

```
SeparatorText("Environment")
  Ambient Light                        <- unchanged, just now under a header
  CollapsingHeader("Fog")
    Fog Mode           combo   Off / Linear / Exponential Height
    ── BeginDisabled(fogMode == Off) ──
    Fog Color          ColorEdit3
    Match Sky Color    checkbox  [tooltip: samples the environment map; needs IBL]
    ── ExponentialHeight only ──
    Density            SliderFloat  0.0 .. 0.2   "%.4f /m"  Logarithmic
    Height Falloff     SliderFloat  0.0 .. 0.5   "%.3f"
    Base Height        DragFloat                 "%.1f m"
    ── Linear only ──
    Start / End        SliderFloat                "%.0f m"
    ── both ──
    Start Distance     SliderFloat  0 .. 100     "%.1f m"
    Maximum Opacity    SliderFloat  0 .. 1       "%.2f"
    Affect Sky         checkbox  [tooltip: horizon haze band]
    TreeNode("Sun Inscattering")
      Sun Intensity    SliderFloat  0 .. 1
      ── BeginDisabled(fogSunIntensity <= 0) ──
      Sun Color        ColorEdit3
      Directional Exp  SliderFloat  1 .. 64      Logarithmic
    TextDisabled("Uses the shadow-casting directional light.")
    ── EndDisabled ──
  CollapsingHeader("Skybox")           <- move the existing one here
─────────────────────────
Graphics Profile
  ... unchanged ...
```

And in the quality section, next to Weather/Particles:

```
Volumetric Fog        checkbox   [Phase 4]
Volumetric Quality    combo Low/Medium/High
```

### Rules to follow (matching what the file already does)

- Every widget ORs into `graphicsChanged` — that's what drives the dirty/save path.
- Wrap dependent controls in `ImGui::BeginDisabled(...)` / `EndDisabled()`, as Bloom,
  Motion Blur, DoF and SSR already do.
- Use `ImGui::IsItemHovered()` + `SetTooltip` for anything non-obvious. Density in
  particular needs one — "extinction per metre at Base Height" is not guessable.
- Density wants `ImGuiSliderFlags_Logarithmic`: the useful range is 0.001–0.05 and a
  linear slider puts all of it in the first 2% of travel.
- Units in the format string (`"%.1f m"`), consistently with the shadow/SSR sliders.

### Scene View debug mode

Add `SceneViewShadingMode::Fog` to the enum in
[`Renderer.h:57`](../ProjectRaceman/src/rendering/Renderer.h), before `Count`.

In `ResolveProfileForTarget` ([`Renderer.cpp:566`](../ProjectRaceman/src/rendering/Renderer.cpp)):

- Add `resolved.fogDebugView = false;` to the block that clears every debug flag for the
  Game View. **This is not optional** — that block is the guarantee that the Game View
  always ships the project's look.
- `case SceneViewShadingMode::Fog:` sets `resolved.fogDebugView = true`.
- Add `resolved.fogMode = FogMode::Off;` to the `disableLighting()` lambda, so Unlit and
  Wireframe stay genuinely unlit. Leave `Plain` alone — Plain is lit-without-post, and
  fog is in-shader lighting, not post.

Debug view output: transmittance as greyscale (white = clear, black = fully fogged),
written in place of `FragColor`.

### Persistence

Mirror the existing pattern exactly:
- **Load:** [`SceneEditorPersistence.cpp:3179–3292`](../ProjectRaceman/src/ui/SceneEditorPersistence.cpp),
  clamping each value on read the way `ssrThickness` (`:3279`) does.
- **Save:** `:3640–3677`, and mind the trailing comma — `ambientColor` is currently the
  last entry and has no comma. Insert fog *before* it or fix the comma.
- `fogMode` is an enum → needs `FogModeFromStorage` / `FogModeToStorage` string helpers
  alongside the existing `RenderStyleFromStorage` / `GraphicsQualityFromStorage`. Use
  strings, not ints, so the JSON survives enum reordering.
- Bump `GraphicsProfile::version` and make sure a project file without a `"fog*"` key
  loads with `FogMode::Off` — existing projects must not suddenly grow fog.

---

## 7. How the rest of the roadmap slots in

Same wiring pattern each time — profile field → preset tier → resolve/debug → render pass
→ UI → persistence. In dependency order:

| Item | Depends on | Settings home |
|---|---|---|
| ~~**Auto-exposure**~~ **DONE** | Compute shader support (histogram) | Graphics Profile, next to Exposure. Manual Exposure is disabled while auto is on; Exposure Compensation (EV) is the artistic offset. |
| **Wet road + decals** | Decal system (new); G-buffer or forward decal pass | Material/surface authoring, **not** the graphics profile. Wetness amount is scene/weather state. |
| **Clustered lighting** | `#include` (Phase 0), SSBO support | Mostly invisible. Profile gets a `maxLightsPerCluster` quality knob; the 8-light cap at `Renderer.cpp:2292` disappears. |
| **Instancing + UBO materials** | Draw-call batching rework in `Flush()` | No user-facing settings. Pure perf. |

Fog first is right: it's self-contained, it forces Phase 0 (which everything else needs),
and it makes the exposure problem visible enough to justify doing auto-exposure next.

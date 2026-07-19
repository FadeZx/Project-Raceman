# Graphics Roadmap

This checklist tracks the renderer features exposed by Project Settings and the order in which the remaining work should be completed.

## Implemented

- [x] HDR intermediate render target and ACES tone mapping
- [x] Exposure control
- [x] Selectable SDR sRGB and HDR linear scRGB outputs
- [x] HDR paper-white and peak-brightness controls with an SDR editor preview
- [x] Windows HDR display/Advanced Color detection and live capability status
- [x] Prefer a 10-bit WGL framebuffer with verified actual channel depth
- [x] FXAA
- [x] Realistic and stylized PBR modes
- [x] Directional shadow mapping
- [x] Configurable shadow resolution and soft-shadow filtering
- [x] Off-screen shadow-caster preservation
- [x] Stabilized cascaded directional shadows with configurable count and distance
- [x] Shadow cascade debug visualization
- [x] HDR bloom with intensity, threshold, and radius controls
- [x] Skybox-driven image-based lighting
- [x] Diffuse irradiance cubemap generation
- [x] Roughness-prefiltered specular environment map
- [x] Integrated BRDF lookup texture
- [x] Reflection intensity, enable/disable, and IBL debug controls
- [x] Roughness-aware screen-space reflections with material-property buffer, adjustable ray budget, edge/distance fading, skybox IBL fallback, and hit debug view
- [x] Mesh LOD selection with project-asset levels, screen-height thresholds, bias, hysteresis, and debug forcing
- [x] Temporal anti-aliasing with stable de-jittered output, 16-frame subpixel sampling, Catmull-Rom reconstruction, logarithmic depth and compact normal rejection, velocity dilation, HDR-aware variance clipping, reactive per-pixel history convergence, sharpening, and camera-cut handling
- [x] Adaptive dynamic resolution with per-viewport frame pacing, quantized scaling, and temporal reset on resize
- [x] Point- and spot-light shadow maps with per-light casting and a configurable local-light budget
- [x] HDR weather atmosphere and quality-scaled procedural weather particles

## Implemented: SSAO

- [x] Expose scene depth and normals as viewport textures
- [x] Generate a deterministic hemisphere sample kernel and noise texture
- [x] Implement the SSAO evaluation pass
- [x] Add edge-aware SSAO blur
- [x] Composite ambient occlusion into the lighting result
- [x] Add radius, bias, intensity, and quality controls
- [x] Add debug visualization and validate Scene/Game render-path parity

## Implemented: Post Processing

- [x] Shared HDR post-processing chain for Scene and Game views
- [x] Per-camera and per-object motion-vector generation
- [x] Velocity-buffer motion blur with shutter, intensity, samples, and radius controls
- [x] Motion-vector debug visualization
- [x] Color grading
- [x] Vignette and film grain
- [x] Depth of field

## Remaining Features

- [x] Native Windows FP16 scRGB DXGI presentation with zero-copy OpenGL/D3D11 interop and SDR fallback
- [x] Point- and spot-light shadows
- [x] Mesh LOD selection
- [x] Temporal anti-aliasing
- [x] Dynamic resolution
- [x] Renderer-integrated weather quality
- [x] Renderer-integrated particle quality
- [x] Graphics presets that configure all implemented effects
- [x] Box reflection-probe components with influence visualization, nearest-probe prioritization, true intensity scaling, blending, parallax correction, and global-skybox fallback
- [x] Baked six-face reflection-probe capture, project-asset storage, persistence, and per-probe PBR sampling
- [ ] Sphere probe influence volumes
- [ ] Selective real-time reflection-probe updates

## Recommended Order

1. Add sphere influence volumes.
2. Add selective real-time probe updates for garages, tunnels, and showcase scenes.

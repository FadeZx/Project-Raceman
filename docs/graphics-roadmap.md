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
- [ ] Color grading
- [ ] Vignette and film grain
- [ ] Depth of field

## Remaining Features

- [x] Native Windows FP16 scRGB DXGI presentation with zero-copy OpenGL/D3D11 interop and SDR fallback
- [ ] Point- and spot-light shadows
- [ ] Mesh LOD selection
- [ ] Temporal anti-aliasing
- [ ] Dynamic resolution
- [ ] Renderer-integrated weather quality
- [ ] Renderer-integrated particle quality
- [ ] Graphics presets that configure all implemented effects

## Recommended Order

1. LOD
2. TAA
3. Dynamic resolution
4. Point/spot shadows and weather/particle quality integration

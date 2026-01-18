# BF3 to ShaderPatch Feature Mapping

This document maps BF3 shader features to their ShaderPatch equivalents for implementation.

## Summary Status

| BF3 Feature | ShaderPatch Status | Notes |
|-------------|-------------------|-------|
| Blinn-Phong Lighting | **IMPLEMENTED** | `lighting_utilities.hlsl` |
| Normal Mapping | **IMPLEMENTED** | `normal_bf3.fx` |
| Specular Mapping | **IMPLEMENTED** | `normal_bf3.fx` |
| Shadow Mapping | **IMPLEMENTED** | Stencil shadow via `SP_USE_STENCIL_SHADOW_MAP` |
| Environment Mapping | **IMPLEMENTED** | `env_map` texture slot t12 |
| Vertex Transformer | **IMPLEMENTED** | Replaces BF3 quaternion unpacking |
| Vertex Colors | **IMPLEMENTED** | Via `Vertex_input.color()` |
| Procedural Clouds | **IMPLEMENTED** | `clouds.fx` with RGBA octave sampling |
| Cube Light Ambient | **APPROXIMATED** | Replaced with gradient ambient |
| Spherical Harmonics | **MISSING** | Not yet ported |
| Detail Mapping | **MISSING** | Needs implementation |
| Lightmaps | **MISSING** | Needs implementation |
| Incandescence | **PARTIAL** | Emissive texture in normal_bf3 |
| Sway Animation | **MISSING** | Needs implementation |
| Soft Particles | **PARTIAL** | OIT handles transparency |

---

## Vertex Attribute Mapping

### BF3 Vertex Inputs

| BF3 Attribute | Semantic | Format |
|---------------|----------|--------|
| `pos` | POSITION | float4 (xyz + W scale) |
| `UVSET0` | TEXCOORD0 | float2 |
| `rgb` | COLOR | float4 (BGRA) |
| `pack_stn` | TEXCOORD1 | float4 (packed quaternion) |
| `UVSET1` | TEXCOORD1 | float2 (conditional) |
| `UVSET3` | TEXCOORD2 | float2 (lightmap UVs) |

### ShaderPatch Equivalent

```hlsl
// ShaderPatch uses Vertex_input struct with accessor methods
struct Vertex_input {
   float3 position();      // Handles decompression from pos.xyz / pos.w
   float3 normal();        // Decompressed from vertex data (replaces quaternion)
   float3 tangent();       // Pre-computed tangent space
   float3 bitangent();     // Pre-computed bitangent
   float4 color();         // SRGB-aware vertex color
   float2 texcoords();     // Primary UVs (handles compression)
};
```

**Key Difference:** BF3 packs tangent space as a quaternion in `pack_stn`, which is expanded via `expandpackedtsb()`. ShaderPatch pre-computes tangent/bitangent in the vertex buffer, accessed via `Transformer`.

### Mapping Rule

```
BF3                          →  ShaderPatch
─────────────────────────────────────────────────────────────
pos.xyz / pos.w              →  Vertex_input.position()
UVSET0                       →  Vertex_input.texcoords()
rgb (BGRA)                   →  Vertex_input.color() (SRGB handled)
pack_stn (quaternion)        →  Transformer.normalWS(), tangentWS(), bitangentWS()
UVSET3 (lightmap)            →  NOT YET MAPPED (needs second UV set)
```

---

## Texture Slot Mapping

### BF3 Samplers

| BF3 Sampler | Purpose |
|-------------|---------|
| `diff` | Diffuse/albedo |
| `detail` | Detail texture |
| `normmap` | Normal map |
| `specmap` | Specular intensity |
| `incan` | Incandescence/glow |
| `lmap` | Lightmap |
| `cubelight` | Environment cube |
| `shadmap` | Shadow depth |

### ShaderPatch Texture Registers

| Register | ShaderPatch Slot | BF3 Equivalent |
|----------|-----------------|----------------|
| t2 | `projected_light_texture` | (spotlight projection) |
| t3 | `shadow_ao_map` | `shadmap` (adapted) |
| t7 | `diffuse_map` | `diff` |
| t8 | `specular_map` | `specmap` |
| t9 | `normal_map` | `normmap` |
| t10 | `ao_map` | (new - no BF3 equiv) |
| t11 | `emissive_map` | `incan` |
| t12 | `env_map` | `cubelight` |
| t13 | `height_map` | (parallax - new) |

### Mapping Rule

```
BF3                          →  ShaderPatch Register
─────────────────────────────────────────────────────────────
diff                         →  t7 (diffuse_map)
normmap                      →  t9 (normal_map, gloss in alpha)
specmap                      →  t8 (specular_map)
incan                        →  t11 (emissive_map)
cubelight                    →  t12 (env_map)
shadmap                      →  t3 (shadow_ao_map.r)
lmap                         →  NOT MAPPED (needs new slot)
detail                       →  NOT MAPPED (needs new slot)
```

---

## Constant Buffer Mapping

### BF3 Constants

| BF3 Constant | Type | Purpose |
|--------------|------|---------|
| `modelviewproj` | float4x4 | MVP matrix |
| `transmodel` | float4x4 | World matrix |
| `uvmtx0` | float4x4 | UV transform |
| `cml[6]` | float4[6] | Cube light faces |
| `sh[9]` | float4[9] | SH coefficients |
| `constantcolour` | float4 | Material color |
| `ambientcolour` | float4 | Ambient color |
| `fogcolour` | float4 | Fog color |
| `compressedhdr` | float2 | HDR scale |
| `depthdata` | float4 | Depth conversion |

### ShaderPatch Constant Buffers

**SceneConstants (b0 VS):**
- `projection_matrix` - Projection only (view*proj in transformer)
- `vs_view_positionWS` - Camera position
- `time` - Animation time
- Fog parameters

**DrawConstants (b1 VS):**
- `world_matrix` (float4x3) - Object to world
- `light_packed_constants[12]` - Dynamic lights
- `custom_constants[9]` - User-defined

**MaterialConstants (MATERIAL_CB_INDEX):**
- Custom per-shader material properties

### Mapping Rule

```
BF3                          →  ShaderPatch
─────────────────────────────────────────────────────────────
modelviewproj                →  Transformer.positionPS() (computed internally)
transmodel                   →  world_matrix (DrawConstants b1)
uvmtx0                       →  custom_constants or Transformer.texcoords()
cml[6]                       →  APPROXIMATED: light::ambient() gradient
sh[9]                        →  NOT MAPPED (needs implementation)
constantcolour               →  material_diffuse_color (DrawConstants)
ambientcolour                →  light_ambient_color_top/bottom (DrawConstants)
fogcolour                    →  fog_color (PSDrawConstants)
compressedhdr                →  lighting_scale (DrawConstants)
depthdata                    →  Handled by depth buffers t1-t2
```

---

## Lighting Model Mapping

### BF3 Cube Light → ShaderPatch Ambient

**BF3 Implementation:**
```hlsl
// 6-direction cube light sampling
float3 cubelightvertex(float3 normal, float4 cml[6]) {
   float3 nsq = normal * normal;
   int3 is_pos = normal > 0;
   float3 color = nsq.x * cml[is_pos.x ? 0 : 3].rgb
                + nsq.y * cml[is_pos.y ? 1 : 4].rgb
                + nsq.z * cml[is_pos.z ? 2 : 5].rgb;
   return color;
}
```

**ShaderPatch Approximation:**
```hlsl
// Gradient ambient based on normal Y
float3 light::ambient(float3 normalWS) {
   float up_factor = normalWS.y * 0.5 + 0.5;
   return lerp(light_ambient_color_bottom, light_ambient_color_top, up_factor);
}
```

**Mapping Note:** The 6-direction cube light provides more directional variation than ShaderPatch's 2-color gradient. For closer fidelity, a full cube light implementation could be added.

### BF3 Blinn-Phong → ShaderPatch Blinn-Phong

**Direct Mapping - Already Implemented:**

```hlsl
// BF3 style
half h = normalize(lightvec + eyevec);
half spec = pow(saturate(dot(h, n)), shininess);

// ShaderPatch (lighting_utilities.hlsl)
light::blinnphong::calculate(diffuse_lighting, specular_lighting,
                             normalWS, viewWS, lightDir,
                             attenuation, light_color, shadow, specular_exp);
```

### BF3 Shadow Mapping → ShaderPatch Stencil Shadow

**BF3:** PCF shadow mapping with 2x2 kernel
**ShaderPatch:** Stencil-based shadow volumes (different technique)

```hlsl
// Enable via static flag
const static bool normal_bf3_use_shadow_map = SP_USE_STENCIL_SHADOW_MAP;

// Shadow applied in do_lighting()
surface.shadow = shadow_ao_sample.r;  // 0 = shadowed, 1 = lit
```

---

## Permutation Flag Mapping

### BF3 Flags → ShaderPatch Static Flags

| BF3 Flag | ShaderPatch Equivalent | Status |
|----------|----------------------|--------|
| `normmap` | Always enabled | Normal mapping standard |
| `specmap` | `NORMAL_BF3_USE_SPECULAR_MAP` | **MAPPED** |
| `shadow` | `SP_USE_STENCIL_SHADOW_MAP` | **MAPPED** |
| `lighting` | `light_active` (runtime) | **MAPPED** |
| `detailmap` | NOT MAPPED | Needs new flag |
| `use_envmap` | `use_env_map` (material bool) | **MAPPED** |
| `use_lmap` | NOT MAPPED | Needs implementation |
| `use_incan` | `use_emissive_texture` (material bool) | **MAPPED** |
| `sway` | NOT MAPPED | Needs implementation |
| `use_trans` | `NORMAL_BF3_USE_TRANSPARENCY` | **MAPPED** |
| `spot_lit` | Handled in light loop | **MAPPED** |
| `unfiltered` | NOT MAPPED | Sampler state control |

---

## Feature Implementation Details

### 1. Detail Mapping (TO IMPLEMENT)

**BF3 Approach:**
```hlsl
// Detail fade based on depth
float detail_fade = saturate(detailparms.z * hpos.z + detailparms.w);
float2 detail_uv = texcoords * detailparms.xy;
float4 detail_color = tex2D(detail, detail_uv);
diffuse_color = lerp(diffuse_color, diffuse_color * detail_color * 2.0, detail_fade);
```

**ShaderPatch Implementation Plan:**
- Add `detail_map` texture at new slot (t14 or t15)
- Add `detail_scale`, `detail_fade_start`, `detail_fade_end` to MaterialConstants
- Apply detail in pixel shader before lighting

### 2. Lightmaps (TO IMPLEMENT)

**BF3 Approach:**
```hlsl
// Second UV set for lightmaps
float3 lightmap = tex2D(lmap, UVSET3).rgb;
diffuse_lighting += lightmap * lightmap_intensity;
```

**ShaderPatch Implementation Plan:**
- Need second UV set support in vertex input
- Add `lightmap` texture slot
- Blend with dynamic lighting

### 3. Sway/Animation (TO IMPLEMENT)

**BF3 Approach:**
- Vertex deformation based on time and position
- Parameters: frequency, amplitude, phase offset

**ShaderPatch Implementation Plan:**
- Add sway parameters to material constants
- Modify world position in vertex shader before transform
- Consider wind direction vector

### 4. Spherical Harmonics (TO IMPLEMENT)

**BF3 Approach:**
```hlsl
float3 shlight(float3 normal, float4 sh[9]) {
   // L2 SH basis evaluation
   float3 color = sh[0].rgb
                + sh[1].rgb * normal.y
                + sh[2].rgb * normal.z
                + sh[3].rgb * normal.x
                + sh[4].rgb * normal.x * normal.y
                + sh[5].rgb * normal.y * normal.z
                + sh[6].rgb * (3.0 * normal.z * normal.z - 1.0)
                + sh[7].rgb * normal.x * normal.z
                + sh[8].rgb * (normal.x * normal.x - normal.y * normal.y);
   return color;
}
```

**ShaderPatch Implementation Plan:**
- Add SH coefficient array to scene constants
- Implement `shlight()` utility function
- Option to use as alternative to cube light

---

## Clouds Implementation (IMPLEMENTED)

The clouds shader demonstrates BF3-style techniques adapted to ShaderPatch:

### BF3 Cloud Techniques Used

1. **RGBA Octave Noise** - Single texture with 4 noise channels:
   - R = Large cloud shapes (low frequency)
   - G = Medium detail (medium frequency)
   - B = Fine detail (high frequency)
   - A = Edge variation (cellular/worley)

2. **Weighted Combination:**
```hlsl
float combined = dot(blended_octaves - 0.5, layer.octave_weights) + 0.5;
```

3. **BF3-style Self-Shadow Lighting:**
```hlsl
// Use noise value as proxy for thickness
float self_shadow = saturate(combined * 0.8 + 0.2);
float view_sun = dot(-ray_dir, -sun_direction);
float sun_facing = base_lit * lerp(0.6, 1.0, self_shadow);
```

### ShaderPatch Adaptations

- Uses postprocess-style pixel shader (no vertex attributes)
- Depth buffer integration for scene intersection
- 3 configurable cloud layers
- Curvature support for planet-scale rendering
- YAML configuration system

---

## Defaults and Fallbacks

### When BF3 Feature is Missing

| Missing Feature | Fallback Behavior |
|-----------------|------------------|
| Cube light `cml[6]` | Use gradient ambient |
| Lightmap | Static lighting from vertex color |
| Detail map | Ignore (diffuse only) |
| SH lighting | Use ambient |
| Sway | Static geometry |

### Material Constant Defaults

```cpp
// normal_bf3_common.hlsl defaults
base_diffuse_color = {1.0, 1.0, 1.0};
base_specular_color = {0.04, 0.04, 0.04};
specular_exponent = 32.0;
gloss_map_weight = 1.0;
use_ao_texture = false;
use_emissive_texture = false;
use_env_map = false;
```

---

## Implementation Checklist

### Phase 1 - Core (COMPLETE)
- [x] Vertex transformer system
- [x] Basic diffuse with ambient
- [x] Normal mapping
- [x] Specular mapping (Blinn-Phong)
- [x] Vertex colors
- [x] Fog integration

### Phase 2 - Lighting (COMPLETE)
- [x] Dynamic directional lights
- [x] Point lights
- [x] Spot lights
- [x] Shadow mapping (stencil-based)
- [x] Environment mapping

### Phase 3 - Advanced (PARTIAL)
- [x] Procedural clouds (BF3 octave style)
- [x] Emissive/incandescence
- [x] Parallax mapping
- [ ] Detail textures
- [ ] Lightmaps (second UV set)

### Phase 4 - Special (NOT STARTED)
- [ ] Spherical harmonics lighting
- [ ] Sway/vegetation animation
- [ ] Full cube light ambient (6-direction)
- [ ] Soft particles with depth

---

## File References

### ShaderPatch Implementation Files

| File | Purpose |
|------|---------|
| `assets/core/src/normal_bf3.fx` | Main BF3 material shader |
| `assets/core/src/normal_bf3_common.hlsl` | Shared material constants and lighting |
| `assets/core/src/clouds.fx` | BF3-style procedural clouds |
| `assets/core/src/lighting_utilities.hlsl` | Blinn-Phong and light iteration |
| `assets/core/src/vertex_transformer.hlsl` | Coordinate space transforms |
| `assets/core/definitions/patch/normal_bf3.json` | Shader definition with states |
| `assets/core/definitions/patch/clouds.json` | Cloud shader definition |
| `src/effects/clouds.hpp` | C++ cloud parameter management |

### BF3 Reference Files

| File | Key Features |
|------|--------------|
| `default/ambient.hlsl` | Base ambient shader template |
| `default/ambient_normal.hlsl` | Normal mapped variant |
| `default/ambient_spec.hlsl` | Specular variant |
| `default/ambient_lightmapped.hlsl` | Lightmap support |
| `*.hlsl` (hex names) | Compiled permutation variants |

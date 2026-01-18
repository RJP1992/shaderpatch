# BF3 Shader Feature List

## Lighting Features

- [ ] **Cube Light Ambient** - 6-direction ambient light evaluation
- [ ] **Lambert Diffuse** - Basic N·L diffuse lighting
- [ ] **Blinn-Phong Specular** - Half-vector specular highlights
- [ ] **Spherical Harmonics (L2)** - 9-coefficient SH lighting
- [ ] **Light Attenuation** - Distance-based falloff
- [ ] **Spotlight Cone** - Angular cone attenuation
- [ ] **Multiple Lights** - Up to 4 dynamic lights

## Shadow Features

- [ ] **Shadow Mapping** - Basic shadow depth comparison
- [ ] **PCF Filtering** - 2x2 percentage closer filtering
- [ ] **Depth Linearization** - Proper depth conversion

## Texture Features

- [ ] **Diffuse Mapping** - Base color texture
- [ ] **Normal Mapping** - Tangent-space normal maps
- [ ] **Specular Mapping** - Specular intensity/power control
- [ ] **Detail Mapping** - Secondary detail texture with fade
- [ ] **Lightmap Sampling** - Pre-baked lighting (UV2)
- [ ] **Environment Mapping** - Cubemap reflections
- [ ] **Incandescence/Glow** - Self-illumination maps
- [ ] **UV Matrix Transform** - Texture coordinate animation

## Vertex Features

- [ ] **Packed Quaternion Normals** - Compressed TBN via quaternion
- [ ] **Position Scaling** - W-component scale factor
- [ ] **Vertex Colors** - Per-vertex RGBA (BGRA on Xbox)
- [ ] **Multiple UV Sets** - UVSET0, UVSET1, UVSET3
- [ ] **Sway/Animation** - Vertex deformation for foliage

## Post-Processing Features

- [ ] **Fog/Atmosphere** - Distance and height-based fog
- [ ] **HDR Compression** - Compressed HDR color handling
- [ ] **Soft Particles** - Depth-based alpha feathering

## Shader Variants

### By Feature Flag
| Flag | Description | Priority |
|------|-------------|----------|
| `normmap` | Normal mapping | High |
| `specmap` | Specular mapping | High |
| `lighting` | Dynamic lights | High |
| `shadow` | Shadow receiving | High |
| `detailmap` | Detail textures | Medium |
| `use_envmap` | Environment maps | Medium |
| `use_lmap` | Lightmaps | Medium |
| `use_incan` | Incandescence | Medium |
| `sway` | Vegetation sway | Low |
| `use_trans` | Transparency | Low |
| `spot_lit` | Spotlight support | Low |
| `unfiltered` | Point sampling | Low |

## Constants Required

### Transform Matrices
- `modelviewproj` (4x4) - MVP matrix
- `transmodel` (4x4) - World matrix
- `uvmtx0` (4x4) - UV transform

### Lighting
- `cml[6]` (float4 array) - Cube light colors
- `sh[9]` (float4 array) - SH coefficients
- `latten0` (float2) - Attenuation power/scale
- `lattencone` (float4) - Spotlight cone params

### Material
- `constantcolour` (float4) - Base color
- `ambientcolour` (float4) - Ambient color
- `fogcolour` (float4) - Fog color/density

### HDR
- `compressedhdr` (float2) - HDR scale factors

### Depth
- `depthdata` (float4) - Depth conversion
- `atmosdata` (float4) - Atmosphere params

## Texture Slots Required

| Slot | Name | Purpose |
|------|------|---------|
| 0 | `diff` | Diffuse/albedo |
| 1 | `detail` | Detail texture |
| 2 | `normmap` | Normal map |
| 3 | `specmap` | Specular map |
| 4 | `incan` | Incandescence |
| 5 | `lmap` | Lightmap |
| 6 | `cubelight` | Environment cube |
| 7 | `shadmap` | Shadow map |

## Implementation Priority

### Phase 1 - Core
1. Basic diffuse with cube light ambient
2. Normal mapping
3. Specular (Blinn-Phong)
4. Vertex colors

### Phase 2 - Lighting
1. Dynamic lighting (Lambert + attenuation)
2. Shadow mapping with PCF
3. Multiple light support

### Phase 3 - Advanced
1. Environment mapping
2. Detail textures
3. Lightmaps
4. Incandescence

### Phase 4 - Special
1. Spherical harmonics
2. Sway/animation
3. Fog/atmosphere
4. Soft particles

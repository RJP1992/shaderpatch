# BF3 Shader Analysis Summary

## Overview

The BF3 pixel shader collection at `D:\BF3 RAW\bf3shaders\pixel` contains 3,954 compiled HLSL shader files from the SWBF3 rendering engine. These are build-time generated shader permutations primarily targeting Xbox 360 (Xenon platform).

## File Organization

- **Total Files:** 3,954 HLSL files
- **Compiled Permutations:** ~3,295 hex-named files (e.g., `00000002.hlsl`, `00000320.hlsl`)
- **Source Templates:** ~20 descriptive files in `default/` subdirectory (ambient.hlsl, ambient_normal.hlsl, etc.)
- **Naming Convention:** Hex names = compiled variants, descriptive names = source templates

## Shader Architecture

All shaders follow a consistent structure:

### 1. Metadata Header (14 lines)
- Shader classification: `default:ambient:UnNamed (default/ambient)`
- Platform: `bgspec=1`
- Feature flags as boolean parameters

### 2. Platform Directives
```hlsl
#define platform_xenon
#define upside_down_textures
```

### 3. Structure Definitions
- `INPUT` - Vertex attributes from vertex buffer
- `TRANS` - Data passed between vertex and pixel shader
- `OUTPUT` - Final fragment color output

### 4. Shared Utility Library (~450 lines)
- Shadow mapping functions
- Lighting calculation utilities
- Color space conversions
- Depth buffer handling
- Texture sampling helpers
- Quaternion/matrix conversions
- Atmosphere/fog calculations
- Spherical harmonics lighting

### 5. Entry Points
- `#if S_VERTEX` block: Vertex shader
- `#if !S_VERTEX` block: Pixel shader

## Permutation System

12 boolean flags control shader variants:

| Flag | Purpose |
|------|---------|
| `shadow` | Dynamic shadow casting/receiving |
| `spot_lit` | Spotlight illumination |
| `lighting` | Dynamic light contribution |
| `sway` | Vegetation/cloth animation |
| `detailmap` | Secondary detail texture layer |
| `unfiltered` | Point vs linear texture sampling |
| `use_envmap` | Environment cube map |
| `specmap` | Specular map texture |
| `normmap` | Normal/bump mapping |
| `use_lmap` | Lightmap texture (UV2) |
| `use_incan` | Self-illumination/incandescence |
| `use_trans` | Transparency/alpha effects |

## Vertex Attributes

### Core (All shaders)
- `UVSET0` (float2, TEXCOORD) - Primary texture coordinates
- `pos` (float4, POSITION) - Position with W scaling component
- `rgb` (float4, COLOR) - Vertex color (BGRA on Xbox)
- `pack_stn` (float4, TEXCOORD1) - Packed tangent/sign/normal as quaternion

### Conditional
- `UVSET1` (float2, TEXCOORD1) - Secondary UV (lighting shaders)
- `UVSET3` (float2, TEXCOORD2) - Lightmap UVs (when `use_lmap=true`)

## Lighting Models

### 1. Cube Light System (Ambient)
- 6-light cube representation for ambient environment
- Per-vertex evaluation using normal direction
- Array of `float4 cml[6]` constants

### 2. Blinn-Phong Specular
- Half-vector: `normalize(lightvec + eyevec)`
- Highlight: `pow(saturate(dot(h, n)), shininess)`
- Backface prevention: only applied when diffuse > 0

### 3. Lambert Diffuse
- `saturate(dot(lightvec, normal))`
- Half-precision variants: `calcdiffuseh()`

### 4. Spherical Harmonics (SH)
- 9-coefficient L2 basis
- Function: `shlight()` evaluates SH basis

### 5. Light Attenuation
- Distance: `a = (dist²)^power * scale`, then `1 - saturate(a)`
- Spotlight cone angle via dot product

### 6. Shadow Mapping
- 2x2 PCF kernel with interpolated weights
- Xenon-specific `tfetch2D` intrinsics
- ±0.5 pixel sample offsets

## Texture Samplers

| Sampler | Purpose | Condition |
|---------|---------|-----------|
| `diff` | Diffuse/albedo | Always |
| `detail` | Secondary detail | `detailmap=true` |
| `incan` | Glow map | `use_incan=true` |
| `specmap` | Specular intensity | `specmap=true` |
| `normmap` | Normal map | `normmap=true` |
| `lmap` | Lightmap | `use_lmap=true` |
| `cubelight` | Environment cube | `use_envmap=true` |
| `shadmap` | Shadow depth | `shadow=true` |

## Constants/Uniforms

### Transformation
- `modelviewproj` (float4x4) - Vertex to screen
- `transmodel` (float4x4) - Object to world
- `uvmtx0` (float4x4) - Texture coordinate matrix

### Lighting
- `cml[6]` (float4[6]) - Cube light faces
- `sh[9]` (float4[9]) - SH coefficients
- `latten0` (float2) - Attenuation params
- `lattencone` (float4) - Spotlight cone

### Material
- `constantcolour` (float4) - Base material color
- `ambientcolour` (float4) - Ambient color
- `fogcolour` (float4) - Fog color/density
- `compressedhdr` (float2) - HDR compression

### Depth/Projection
- `depthdata` (float4) - Depth conversion
- `atmosdata` (float4) - Fog distance
- `window` (float4) - Viewport mapping

## Engine Conventions

### Coordinate System
- Right-handed
- Y-up
- Positions normalized by W after transform
- Projected positions [-1, 1] before viewport

### Depth
- Linear depth: `z1 = (depthdata.y) / (screenz - depthdata.x)`
- Forward depth buffer (closer = lower)

### Texture Space
- Platform flip: Y inverted on Xenon
- RGB↔BGR swizzle for colors on Xenon
- [0, 1] address range

### HDR/Color
- Compressed HDR: multiply by `compressedhdr.x` (~0.125)
- Linear space output expected
- Gamma handled at final stage

### Normal/Tangent
- Packed quaternion in `pack_stn`
- Expanded to 3x3 TBN matrix in vertex shader
- Sign bit for binormal direction

## Key Utility Functions

### Fast Approximations
```hlsl
fastpow(float x, a, b): (saturate(a*x + b))^4
fasterpow(float x, a): (saturate(a*x + 1-a))^4
```

### Quaternion to Matrix
```hlsl
expandpackedtsb(out float3x3 m, float4 pkd)
```

### Depth Softness
```hlsl
depthfeatherparticle(depthmap, depthdata, screenpos, featherdistance)
```

## Shader Categories

1. **Basic Unlit** - Diffuse + vertex color + cube light ambient
2. **Filtered/Unfiltered** - Point vs linear sampling variants
3. **Normal-Mapped** - Per-pixel normals from texture
4. **Dynamic Lighting** - Per-vertex light evaluation
5. **Shadow-Receiving** - Shadow map PCF sampling
6. **Lightmapped** - Pre-baked lightmap lookup
7. **Swaying** - Vertex deformation for foliage
8. **Incandescence** - Self-illumination/glow
9. **Environment Mapped** - Cube map reflections
10. **Specular Mapped** - Texture-based specular control

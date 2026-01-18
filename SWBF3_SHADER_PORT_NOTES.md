# SWBF3 Shader Porting Project

## Project Overview
Porting atmosphere/fog shaders from cancelled Star Wars Battlefront 3 (2008) to shaderpatch for SWBF2 Classic.

## Source Material
- **Location**: `D:\BF3 RAW\bf3shaders\pixel`
- **Format**: HLSL files (combined vertex + pixel shaders)
- **Count**: 3,327 pixel shaders
- **Original Platform**: Xbox 360 (Xenon) / PS3 (Cell)
- **Date**: September 2008

## Current Focus: Atmosphere/Fog System

### SWBF3 Atmosphere Implementation (from analysis of 00000003.hlsl, 00000004.hlsl)

Key features of the SWBF3 atmosphere system:

1. **Cubemap-based atmosphere color**
   - Uses `samplerCUBE atmoscubemap` for sky/atmosphere color lookup
   - View direction determines atmosphere color (realistic sky gradient)

2. **Height-aware ray marching (simplified)**
   ```hlsl
   // From vertex shader - calculates atmosphere along view ray
   float3 dir = worldpos.xyz - worldviewpos;
   float t = (atmosdata.y - worldviewpos.y) / dir.y;  // intersect with horizon plane
   dir *= (dir.y>0)? saturate(t) : saturate(1-t);     // clamp ray to horizon
   float dist = length(dir);
   float atmos = sqrt(dist) * atmosdata.x;            // sqrt falloff for natural look
   float heightdiff = saturate(2.0 * dir.y/atmosdata.y - 1.0);
   atmos = lerp(atmos, atmosdata.z, saturate(heightdiff * atmosdata.w));
   ```

3. **atmosdata uniform parameters**
   - `atmosdata.x` - density scale (multiplied by sqrt of distance)
   - `atmosdata.y` - horizon height/atmospheric ceiling
   - `atmosdata.z` - maximum atmosphere value (clamp)
   - `atmosdata.w` - height falloff multiplier

4. **Dual fog layers**
   - Atmosphere layer: cubemap-sampled, height-aware
   - Ground fog layer: flat color with depth-based intensity
   ```hlsl
   // In pixel shader
   float3 atmoscolour = texCUBE(atmoscubemap, trans.filterdata.xyz).rgb;
   output.col.rgb = lerp(output.col.rgb, atmoscolour, trans.filterdata.w);
   output.col.rgb = lerp(output.col.rgb, fogcolour.xyz, fogcolour.w * trans.filterdata.z);
   ```

5. **Per-vertex efficiency**
   - Most calculations done in vertex shader
   - Only cubemap lookup + lerps in pixel shader

### Current Shaderpatch Fog (postprocess_fog.fx)

Issues with current implementation:
- Post-process only (no per-vertex option)
- Linear distance fog (not physically-based sqrt falloff)
- No cubemap atmosphere lookup
- Height fog is simple exponential falloff
- Missing the horizon intersection calculation

### Improvement Plan

**Option A: Enhanced Post-Process**
- Add cubemap atmosphere lookup to post-process
- Implement SWBF3-style sqrt distance falloff
- Add horizon intersection calculation for height

**Option B: Per-Material Atmosphere (like SWBF3)**
- Add atmosphere calculations to material shaders (normal_bf3.fx etc.)
- Requires texture slot for atmosphere cubemap
- More authentic to original but more invasive

**Option C: Hybrid**
- Per-material atmosphere for near objects
- Post-process for distant fog fill

## Utility Functions to Port

From SWBF3 shaders, useful functions:

```hlsl
// Atmosphere cubemap lookup with horizon offset
float4 calcatmosphere(samplerCUBE cubemap, float3 viewdir, float horizon, float density, float falloff)
{
    float dist = length(viewdir);
    float3 lookup = viewdir/dist;
    lookup.y -= horizon * (1.0 - lookup.y);  // horizon offset
    float4 atmoscolour = texCUBE(cubemap, lookup);
    atmoscolour.a = saturate(pow(dist, falloff) * density);
    return atmoscolour;
}

// HDR encoding/decoding
float4 encodehdr(float4 source, float compress);
float4 decodehdr(float4 source, float compressinverse);

// Cube light (SH approximation)
float3 cubelight(float3 n, float4 sh[6], float mult);
```

## Files to Modify

### For Enhanced Post-Process Approach:
- `assets/core/src/postprocess_fog.fx` - Main fog shader
- `src/effects/postprocess.cpp` - C++ fog pass
- `src/effects/postprocess_params.hpp` - Fog_params struct

### For Per-Material Approach:
- `assets/core/src/normal_bf3.fx` - BF3 material shader
- `assets/core/src/lighting_bf3.hlsl` - Lighting utilities

## Session Notes

### Session 1
- Analyzed SWBF3 shader structure
- Identified 3327 shaders, many with atmosphere code
- Key insight: SWBF3 uses sqrt(distance) falloff + horizon plane intersection
- Current shaderpatch fog is simpler linear distance + exponential height
- Created this tracking document

### Session 2 (Completed)
- Implemented enhanced post-process fog with SWBF3 algorithm
- Added atmosphere cubemap support (optional)
- Added new parameters: horizon_height, atmos_density, max_atmosphere, atmos_height_falloff, horizon_offset
- Added ImGui controls for all new parameters
- Files modified:
  - `src/effects/postprocess_params.hpp` - New Fog_params members + YAML
  - `src/effects/postprocess.cpp` - Fog_constants struct + cubemap binding
  - `assets/core/src/postprocess_fog.fx` - SWBF3 algorithm implementation
  - `src/effects/control.cpp` - ImGui controls

### Next Steps
1. Build and test the project
2. Tune default parameter values based on in-game testing
3. (Optional) Create a default atmosphere cubemap texture

## Reference Shaders

SWBF3 shaders with atmosphere:
- `00000002.hlsl` - detailmap + atmosphere
- `00000003.hlsl` - unfiltered + atmosphere + fog
- `00000004.hlsl` - sway (foliage) + atmosphere + fog
- Many more...

## How to Resume This Project

When resuming in Claude Code:
1. Read this file: `C:\Users\tehpa\source\repos\shaderpatch\SWBF3_SHADER_PORT_NOTES.md`
2. SWBF3 shaders are at: `D:\BF3 RAW\bf3shaders\pixel`
3. Current fog implementation: `assets/core/src/postprocess_fog.fx`
4. C++ fog code: `src/effects/postprocess.cpp` (do_fog function)

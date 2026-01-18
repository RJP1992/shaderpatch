# ShaderPatch Shader System Summary

## Overview

ShaderPatch uses a JSON-driven shader definition system with HLSL source files. The framework provides standardized constant buffers, vertex input abstractions, and a permutation system based on static flags.

## Directory Structure

```
assets/core/
├── src/                      # Shader source files (.fx, .hlsl)
│   ├── *.fx                  # Complete shader implementations
│   └── *.hlsl                # Shared includes and utilities
├── definitions/
│   ├── *.json                # Core shader definitions
│   └── patch/
│       └── *.json            # Extended/ported shader definitions
└── textures/                 # Built-in textures

src/
├── shader/                   # C++ shader database and compilation
└── effects/                  # C++ effect management and parameter binding
```

## JSON Definition Format

```json
{
  "group_name": "shader_group_name",
  "source_name": "shader_file.fx",

  "input_layouts": { ... },

  "entrypoints": {
    "main_vs": {
      "stage": "vertex",
      "vertex_state": {
        "input_layout": "$auto",
        "generic_input": {
          "dynamic_compression": true,
          "position": true,
          "skinned": true,
          "normal": true,
          "tangents": true,
          "color": true,
          "texture_coords": true
        }
      },
      "static_flags": ["FLAG_1", "FLAG_2"],
      "defines": [["MACRO", "value"]]
    }
  },

  "rendertypes": { "rendertype_name": {} },

  "states": {
    "state_name": {
      "type": "standard",
      "vertex_shader": {
        "entrypoint": "main_vs",
        "static_flags": {"FLAG_1": true}
      },
      "pixel_shader": {
        "entrypoint": "main_ps",
        "static_flags": {"FLAG_1": true}
      }
    }
  }
}
```

## Constant Buffer Architecture

### Vertex Shader

| Buffer | Register | Contents |
|--------|----------|----------|
| SceneConstants | b0 | projection_matrix, vs_view_positionWS, fog params, time, shadow transform |
| DrawConstants | b1 | compression params, world_matrix, ambient colors, light_packed_constants[12], custom_constants[9] |
| TeamColorConstants | b2 | Friend/foe colors |
| FixedfuncConstants | b3 | Legacy fixed-function |
| MaterialConstants | b4 | Shader-specific material params |

### Pixel Shader

| Buffer | Register | Contents |
|--------|----------|----------|
| PSDrawConstants | b0 | ps_custom_constants[5], rt_resolution, fog color, lighting flags, time |
| PSSceneConstants | b1 | Scene-wide pixel shader constants |
| PSFixedfuncConstants | b2 | Legacy fixed-function |
| MaterialConstants | b3 | Shader-specific material params |

## Texture Binding

### Pixel Shader Standard Slots

| Register | Purpose |
|----------|---------|
| t0 | Scene input (postprocess) |
| t1-t6 | System (depth_near, depth_far, etc.) |
| t4 | Cube projected texture (always available) |
| t7 | Diffuse/color map |
| t8 | Specular map |
| t9 | Normal map (gloss in alpha) |
| t10 | Ambient occlusion |
| t11 | Emissive map |
| t12 | Environment cubemap |
| t13 | Height/displacement |

### Sampler Slots (Fixed)

| Register | Sampler |
|----------|---------|
| s0 | aniso_wrap_sampler |
| s1 | linear_clamp_sampler |
| s2 | linear_wrap_sampler |
| s3 | linear_mirror_sampler |
| s4 | text_sampler |
| s5 | projtex_sampler |

## Vertex Input System

### Vertex_input Struct

```hlsl
struct Vertex_input {
  float3 position();           // Handles decompression
  float3 blend_weights();
  int3 bone_index();
  float3 normal();             // Handles decompression
  float3 tangent();
  float3 bitangent();
  float4 color();              // SRGB-aware
  float2 texcoords();          // Handles compression
};
```

### Generic Input Flags

- `dynamic_compression` - Runtime position/texcoord compression
- `position` - Include position
- `skinned` - Bone weights and indices
- `normal` - Vertex normals
- `tangents` - Tangent/bitangent
- `color` - Vertex colors
- `texture_coords` - UVs

## Transformer System

```hlsl
interface Transformer {
  float3 positionOS();    // Object space
  float3 positionWS();    // World space
  float4 positionPS();    // Projection space
  float3 normalOS();
  float3 normalWS();
  float3 tangentOS();
  float3 tangentWS();
  float3 bitangentOS();
  float3 bitangentWS();
};

// Usage:
Transformer transformer = create_transformer(input);
float3 positionWS = transformer.positionWS();
float4 positionPS = transformer.positionPS();
```

## Static Flag System

1. Flags declared in JSON as string arrays
2. Compiled into multiple shader variants
3. Runtime selection via 64-bit bitmask
4. Preprocessor receives: `#define FLAG_NAME 1` or `#define FLAG_NAME 0`

Example flags from normal_bf3:
- `NORMAL_BF3_USE_DYNAMIC_TANGENTS`
- `NORMAL_BF3_USE_TRANSPARENCY`
- `NORMAL_BF3_USE_SPECULAR_MAP`
- `NORMAL_BF3_USE_PARALLAX_MAPPING`
- `NORMAL_BF3_USE_HARDEDGED_TEST`
- `SP_USE_STENCIL_SHADOW_MAP`
- `SP_USE_PROJECTED_TEXTURE`

## Utility Includes

| File | Purpose |
|------|---------|
| constants_list.hlsl | All constant buffers, light helpers |
| vertex_utilities.hlsl | Material color, texture transforms, fog |
| pixel_utilities.hlsl | Normal sampling, parallax, TBN conversion |
| vertex_transformer.hlsl | Coordinate space transformations |
| generic_vertex_input.hlsl | Vertex input with compression |
| adaptive_oit.hlsl | Order-independent transparency |
| pixel_sampler_states.hlsl | Sampler declarations |
| lighting_utilities.hlsl | Light calculations, Blinn-Phong |
| lighting_pbr.hlsl | Physically-based rendering |
| color_utilities.hlsl | SRGB conversions |

## Adding a New Shader

### 1. Create Shader File
`assets/core/src/my_shader.fx`
```hlsl
#include "generic_vertex_input.hlsl"
#include "vertex_transformer.hlsl"
#include "constants_list.hlsl"

cbuffer MaterialConstants : register(MATERIAL_CB_INDEX) {
  // Material params
};

Vs_output main_vs(Vertex_input input) {
  Transformer transformer = create_transformer(input);
  // ...
}

float4 main_ps(Ps_input input) : SV_Target0 {
  // ...
}
```

### 2. Create Definition
`assets/core/definitions/patch/my_shader.json`
```json
{
  "group_name": "my_shader",
  "source_name": "my_shader.fx",
  "entrypoints": { ... },
  "rendertypes": { ... },
  "states": { ... }
}
```

### 3. Add to Build
- Add `.json` to `assets/core/core.vcxproj`
- Add `.fx` to project filters

### 4. Optional C++ Integration
Create effect class in `src/effects/` if runtime parameter management needed.

## OIT Support

For transparent shaders:
```hlsl
[earlydepthstencil]
void oit_main_ps(Ps_input input, float4 positionSS : SV_Position, uint coverage : SV_Coverage) {
  const float4 color = compute_final_color();
  aoit::write_pixel((uint2)positionSS.xy, positionSS.z, color, coverage);
}
```

Declare `pixel_oit_shader` entrypoint in JSON state definition.

## Coordinate Conventions

- **World Space** - Game world, used for lighting
- **Object Space** - Per-instance local, often compressed
- **Tangent Space** - Normal map relative (X=Right, Y=Up, Z=Out)
- **Screen Space** - 2D pixel coordinates

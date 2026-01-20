#ifndef FOG_COMMON_INCLUDED
#define FOG_COMMON_INCLUDED

// SWBF3-style post-process fog constants
// Based on SceneVolumeData template parameters

cbuffer FogConstants : register(b4)
{
   // Inverse view matrix for world position reconstruction
   float4x4 inv_view_matrix;

   // Distance fog - SWBF3: fog[], fogNear, fogFar
   float4 fog_color;           // fog[0-3]: RGB + intensity/alpha
   float fog_start;            // fogNear
   float fog_end;              // fogFar

   // Height/Atmospheric fog - SWBF3: fogMinHeight, fogMaxHeight, fogDensity, fogAlpha
   float height_base;          // fogMinHeight
   float height_ceiling;       // fogMaxHeight
   float atmos_density;        // fogDensity
   float fog_opacity;          // fogAlpha

   // Projection params for view-space reconstruction (focal length)
   float proj_scale_x;         // projection[0][0]
   float proj_scale_y;         // projection[1][1]

   // Camera info
   float3 camera_position;
   float time;

   // Options - SWBF3: fogAdd, fogSky
   uint blend_additive;        // fogAdd: 0=lerp, 1=additive
   uint apply_to_sky;          // fogSky: apply fog to sky

   // Depth linearization params (extracted from projection matrix)
   float2 depth_linearize_params;

   // Padding to 16-byte alignment
   float2 _padding2;
}

// Samplers
SamplerState point_clamp_sampler : register(s0);

// Linearize perspective depth to view-space Z distance.
// Uses precomputed params from projection matrix (same as debug_visualizer).
// Formula: linear_z = params.x / (params.y - raw_depth)
float linearize_depth(float raw_depth)
{
   return depth_linearize_params.x / (depth_linearize_params.y - raw_depth);
}

// Reconstruct world position from UV and linear depth
// Uses view-space reconstruction then transforms to world space
float3 reconstruct_world_position(float2 uv, float linear_depth)
{
   // Convert UV to NDC [-1, 1]
   float2 ndc;
   ndc.x = uv.x * 2.0 - 1.0;
   ndc.y = (1.0 - uv.y) * 2.0 - 1.0;  // Flip Y for D3D

   // Reconstruct view-space position using projection parameters
   // view_xy = ndc_xy * view_z / proj_scale
   float3 view_pos;
   view_pos.x = ndc.x * linear_depth / proj_scale_x;
   view_pos.y = ndc.y * linear_depth / proj_scale_y;
   view_pos.z = linear_depth;

   // Transform view-space to world-space using inverse view matrix
   float3 world_pos = mul(float4(view_pos, 1.0), inv_view_matrix).xyz;

   return world_pos;
}

// Calculate distance fog factor (SWBF3 formula)
// Formula: saturate((dist - fogNear) / (fogFar - fogNear)) * fog[3]
float calculate_distance_fog(float distance)
{
   float fog_range = fog_end - fog_start;
   if (fog_range < 0.001) return 0.0;

   float factor = saturate((distance - fog_start) / fog_range);
   return factor * fog_color.a;  // fog_color.a = fog[3] = intensity
}

// Calculate height-based atmospheric fog (SWBF3 formula)
// Uses sqrt falloff for characteristic SWBF3 look
// Height is in absolute world Y coordinates (static fog layer)
// Formula: atmosphere = sqrt(dist) * fogDensity * height_factor * fogAlpha
float calculate_height_fog(float3 world_pos, float distance)
{
   float height_range = height_ceiling - height_base;
   if (height_range < 0.001) return 0.0;

   // Height factor: height_base is the lowest fog (full density),
   // height_ceiling is highest (no fog)
   float height_factor = saturate((world_pos.y - height_base) / height_range);
   height_factor = 1.0 - height_factor;  // Invert: full fog at base, none at ceiling

   // SWBF3 atmospheric fog formula:
   // sqrt(distance) for smooth falloff, density controls rate,
   // fog_opacity (fogAlpha) is the intensity multiplier
   float atmos = sqrt(distance) * atmos_density * height_factor * fog_opacity;

   return saturate(atmos);
}

// Combined fog calculation
float calculate_fog_factor(float3 world_pos)
{
   float distance = length(world_pos - camera_position);

   float dist_fog = calculate_distance_fog(distance);
   float height_fog = calculate_height_fog(world_pos, distance);

   // Use maximum of distance and height fog
   return saturate(max(dist_fog, height_fog));
}

// Apply fog to color based on blend mode (SWBF3: fogAdd)
float3 apply_fog_color(float3 color, float fog_factor)
{
   if (blend_additive) {
      // Additive blend (fogAdd = true)
      return color + fog_color.rgb * fog_factor;
   }
   else {
      // Alpha blend (fogAdd = false)
      return lerp(color, fog_color.rgb, fog_factor);
   }
}

#endif

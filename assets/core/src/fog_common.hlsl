#ifndef FOG_COMMON_INCLUDED
#define FOG_COMMON_INCLUDED

// SWBF3-style post-process fog constants
// Based on SceneVolumeData template parameters
// Prefixed with pp_ to avoid conflicts with game constants in constants_list.hlsl

cbuffer FogConstants : register(b4)
{
   // Inverse view matrix for world position reconstruction
   float4x4 pp_inv_view_matrix;

   // Distance fog - SWBF3: fog[], fogNear, fogFar
   float4 pp_fog_color;           // fog[0-3]: RGB + intensity/alpha
   float pp_fog_start;            // fogNear
   float pp_fog_end;              // fogFar

   // Height/Atmospheric fog - SWBF3: fogMinHeight, fogMaxHeight, fogDensity, fogAlpha
   float pp_height_base;          // fogMinHeight
   float pp_height_ceiling;       // fogMaxHeight
   float pp_atmos_density;        // fogDensity (atmosdata.x)
   float pp_fog_alpha;            // fogAlpha (atmosdata.z) - min atmosphere for above-layer objects

   // Projection params for view-space reconstruction (focal length)
   float pp_proj_scale_x;         // projection[0][0]
   float pp_proj_scale_y;         // projection[1][1]

   // Camera info
   float3 pp_camera_position;
   float pp_time;

   // Options - SWBF3: fogAdd, fogSky
   uint pp_blend_additive;        // fogAdd: 0=lerp, 1=additive
   uint pp_apply_to_sky;          // fogSky: apply fog to sky

   // Depth linearization params (extracted from projection matrix)
   float2 pp_depth_linearize_params;

   // Height falloff: controls blend rate to min atmosphere for above-layer rays (atmosdata.w)
   float pp_height_falloff;

   // Immersion: adds near-field fog when camera is in fog layer (0 = off, 1 = full)
   float pp_fog_immersion;

   // Immersion distance curve
   float pp_immersion_start;  // Distance where immersion fog starts fading in
   float pp_immersion_end;    // Distance where immersion fog reaches full strength
   float pp_immersion_range;  // Distance where immersion fog fades out to 0

   // Height fog distance range option: 0 = ignore fog_start/end, 1 = respect them
   uint pp_height_fog_use_distance_range;

   // Ceiling fade: distance above ceiling where fog smoothly fades to 0
   // Compensates for per-pixel precision vs SWBF3's per-vertex interpolation
   float pp_ceiling_fade;

   // Fog disc boundary: limits fog to a circular area in XZ
   float pp_fog_disc_center_x;
   float pp_fog_disc_center_z;
   float pp_fog_disc_radius;      // 0 = disabled (infinite fog)
   float pp_fog_disc_edge_fade;   // Fade distance at disc edge
}

// Samplers
SamplerState point_clamp_sampler : register(s0);

// Linearize perspective depth to view-space Z distance.
// Uses precomputed params from projection matrix (same as debug_visualizer).
// Formula: linear_z = params.x / (params.y - raw_depth)
float linearize_depth(float raw_depth)
{
   return pp_depth_linearize_params.x / (pp_depth_linearize_params.y - raw_depth);
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
   view_pos.x = ndc.x * linear_depth / pp_proj_scale_x;
   view_pos.y = ndc.y * linear_depth / pp_proj_scale_y;
   view_pos.z = linear_depth;

   // Transform view-space to world-space using inverse view matrix
   float3 world_pos = mul(float4(view_pos, 1.0), pp_inv_view_matrix).xyz;

   return world_pos;
}

// Calculate distance fog factor (SWBF3 formula)
// Formula: saturate((dist - fogNear) / (fogFar - fogNear)) * fog[3]
float calculate_distance_fog(float distance)
{
   float fog_range = pp_fog_end - pp_fog_start;
   if (fog_range < 0.001) return 0.0;

   float factor = saturate((distance - pp_fog_start) / fog_range);
   return factor * pp_fog_color.a;  // pp_fog_color.a = fog[3] = intensity
}

// Calculate how deep the camera is within the fog layer (0 = outside/above, 1 = at base)
float calculate_camera_immersion()
{
   float height_range = pp_height_ceiling - pp_height_base;
   if (height_range < 0.001) return 0.0;

   // Camera depth in fog layer: 0 at ceiling, 1 at base
   float camera_depth = saturate((pp_height_ceiling - pp_camera_position.y) / height_range);
   return camera_depth;
}

// Calculate height-based atmospheric fog (SWBF3 ray-through-layer model)
// Instead of using the object's height to determine fog, this measures how much
// of the view ray passes through the fog layer [pp_height_base, pp_height_ceiling].
// Objects above the fog layer still accumulate fog because the view ray crosses
// through the layer on the way to them.
//
// SWBF3 shader reference (vertex shader atmosphere calc):
//   dir = worldpos - worldviewpos
//   t = (atmosdata.y - worldviewpos.y) / dir.y
//   dir *= (dir.y>0) ? saturate(t) : saturate(1-t)
//   dist = length(dir)
//   atmos = sqrt(dist) * atmosdata.x
//   heightdiff = saturate(2.0 * dir.y / atmosdata.y - 1.0)
//   atmos = lerp(atmos, atmosdata.z, saturate(heightdiff * atmosdata.w))
//
float calculate_height_fog(float3 world_pos, float distance)
{
   float height_range = pp_height_ceiling - pp_height_base;
   if (height_range < 0.001) return 0.0;

   // If using distance range, apply the "bubble" - no fog before fog_start
   if (pp_height_fog_use_distance_range) {
      float fog_range = pp_fog_end - pp_fog_start;
      if (fog_range < 0.001 || distance < pp_fog_start) return 0.0;
   }

   // Ray from camera to object
   float3 dir = world_pos - pp_camera_position;
   float total_dist = length(dir);

   // Clip the ray at the ceiling only (SWBF3 behavior - fog extends infinitely below)
   // t=0 is camera position, t=1 is the object position.
   float t_enter, t_exit;

   if (abs(dir.y) > 0.001) {
      float t_ceiling = (pp_height_ceiling - pp_camera_position.y) / dir.y;

      if (dir.y > 0.0) {
         // Ray going up: fog region is from camera to ceiling intersection
         t_enter = 0.0;
         t_exit = saturate(t_ceiling);
      }
      else {
         // Ray going down: fog region is from ceiling intersection to object
         t_enter = saturate(t_ceiling);
         t_exit = 1.0;
      }
   }
   else {
      // Nearly horizontal ray - fog if camera is at or below ceiling
      bool below_ceiling = (pp_camera_position.y <= pp_height_ceiling);
      t_enter = below_ceiling ? 0.0 : 1.0;
      t_exit = below_ceiling ? 1.0 : 0.0;
   }

   // No intersection - ray is entirely above the ceiling
   if (t_enter >= t_exit) {
      float result = 0.0;

      if (pp_fog_immersion > 0.0) {
         float camera_immersion = calculate_camera_immersion();
         float near_fog = camera_immersion * pp_fog_immersion;
         float near_blend = 0.0;
         if (distance >= pp_immersion_start && distance < pp_immersion_end) {
            float fade_range = pp_immersion_end - pp_immersion_start;
            near_blend = (fade_range > 0.001) ? saturate((distance - pp_immersion_start) / fade_range) : 1.0;
         }
         else if (distance >= pp_immersion_end && distance < pp_immersion_range) {
            float fade_range = pp_immersion_range - pp_immersion_end;
            near_blend = (fade_range > 0.001) ? saturate(1.0 - (distance - pp_immersion_end) / fade_range) : 0.0;
         }
         result += near_fog * near_blend;
      }
      return saturate(result);
   }

   // Path length through the fog layer
   float fog_path = total_dist * (t_exit - t_enter);

   // SWBF3 atmospheric fog: sqrt(path) * density
   float atmos = sqrt(fog_path) * pp_atmos_density;

   // Residual atmosphere for above-layer objects (SWBF3 fogAlpha / atmosdata.z)
   // Only applies to UPWARD rays - looking up through the layer at objects above.
   // SWBF3: heightdiff = saturate(2.0 * dir.y / ceiling - 1.0)
   // For downward rays (dir.y < 0), heightdiff = 0, fogAlpha has no effect.
   if (dir.y > 0.001) {
      float vertical_extent = dir.y * (t_exit - t_enter);
      float vertical_fraction = vertical_extent / height_range;
      float heightdiff = saturate(2.0 * vertical_fraction - 1.0);
      atmos = lerp(atmos, pp_fog_alpha, saturate(heightdiff * pp_height_falloff));
   }

   // Ceiling fade: taper fog near the top of the layer when viewed from above
   // Creates a soft top edge on the fog blanket without adding fog above the layer
   if (pp_ceiling_fade > 0.001 && pp_camera_position.y > pp_height_ceiling) {
      float depth_below_ceiling = pp_height_ceiling - world_pos.y;
      if (depth_below_ceiling >= 0.0 && depth_below_ceiling < pp_ceiling_fade) {
         atmos *= saturate(depth_below_ceiling / pp_ceiling_fade);
      }
   }

   // If using distance range, ramp the fog from start to end
   if (pp_height_fog_use_distance_range) {
      float fog_range = pp_fog_end - pp_fog_start;
      float range_factor = saturate((distance - pp_fog_start) / fog_range);
      atmos *= range_factor;
   }

   // Immersion: when camera is in the fog layer, add baseline fog to nearby objects
   // This makes it feel thicker when you're standing in the fog
   if (pp_fog_immersion > 0.0) {
      float camera_immersion = calculate_camera_immersion();
      // Add near-field fog based on how deep camera is in fog
      float near_fog = camera_immersion * pp_fog_immersion;

      // Calculate distance blend with start/end/range curve:
      // 0 to start: no fog, start to end: fade in, end to range: fade out
      float near_blend = 0.0;
      if (distance < pp_immersion_start) {
         near_blend = 0.0;
      }
      else if (distance < pp_immersion_end) {
         float fade_range = pp_immersion_end - pp_immersion_start;
         near_blend = (fade_range > 0.001) ? saturate((distance - pp_immersion_start) / fade_range) : 1.0;
      }
      else if (distance < pp_immersion_range) {
         float fade_range = pp_immersion_range - pp_immersion_end;
         near_blend = (fade_range > 0.001) ? saturate(1.0 - (distance - pp_immersion_end) / fade_range) : 0.0;
      }

      atmos += near_fog * near_blend;
   }

   return saturate(atmos);
}

// Calculate disc boundary fade (1 = inside disc, 0 = outside)
float calculate_disc_fade(float3 world_pos)
{
   if (pp_fog_disc_radius < 0.001) return 1.0;  // Disabled = infinite fog

   float2 xz_offset = world_pos.xz - float2(pp_fog_disc_center_x, pp_fog_disc_center_z);
   float xz_dist = length(xz_offset);
   float edge_fade = max(pp_fog_disc_edge_fade, 0.001);
   return 1.0 - saturate((xz_dist - pp_fog_disc_radius) / edge_fade);
}

// Combined fog calculation
float calculate_fog_factor(float3 world_pos)
{
   float distance = length(world_pos - pp_camera_position);

   float dist_fog = calculate_distance_fog(distance);
   float height_fog = calculate_height_fog(world_pos, distance);

   // Use maximum of distance and height fog
   return saturate(max(dist_fog, height_fog));
}

// Apply fog to color based on blend mode (SWBF3: fogAdd)
float3 apply_fog_to_color(float3 color, float fog_factor)
{
   if (pp_blend_additive) {
      // Additive blend (fogAdd = true)
      return color + pp_fog_color.rgb * fog_factor;
   }
   else {
      // Alpha blend (fogAdd = false)
      return lerp(color, pp_fog_color.rgb, fog_factor);
   }
}

// Calculate fog for a given UV and raw depth (for use in OIT resolve)
// Returns the fog factor and applies it to the color
float3 apply_fog_for_depth(float3 color, float2 uv, float raw_depth)
{
   // Skip sky pixels
   if (raw_depth > 0.99999) return color;

   float linear_depth = linearize_depth(raw_depth);
   float3 world_pos = reconstruct_world_position(uv, linear_depth);

   float dist_fog = calculate_distance_fog(linear_depth);
   float height_fog = calculate_height_fog(world_pos, linear_depth);
   float fog_factor = saturate(dist_fog + height_fog);

   if (fog_factor < 0.001) return color;

   return apply_fog_to_color(color, fog_factor);
}

#endif

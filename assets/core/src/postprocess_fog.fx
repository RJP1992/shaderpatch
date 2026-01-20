// SWBF3-style post-process fog shader
// Applies distance and height-based fog to the scene after geometry rendering

#include "fog_common.hlsl"

// clang-format off

#pragma warning(disable : 3571)

// Scene color and depth textures
Texture2D<float4> scene_texture : register(t0);
Texture2D<float> depth_texture : register(t1);

// Fullscreen triangle vertex shader
// Generates a fullscreen triangle from vertex ID (0, 1, 2)
struct Vs_output
{
   float2 texcoords : TEXCOORD;
   float4 positionPS : SV_Position;
};

Vs_output main_vs(uint id : SV_VertexID)
{
   Vs_output output;

   // Generate fullscreen triangle vertices
   // id=0: (-1, -1), id=1: (-1, 3), id=2: (3, -1)
   if (id == 0) {
      output.positionPS = float4(-1.0, -1.0, 0.0, 1.0);
      output.texcoords = float2(0.0, 1.0);
   }
   else if (id == 1) {
      output.positionPS = float4(-1.0, 3.0, 0.0, 1.0);
      output.texcoords = float2(0.0, -1.0);
   }
   else {
      output.positionPS = float4(3.0, -1.0, 0.0, 1.0);
      output.texcoords = float2(2.0, 1.0);
   }

   return output;
}

// Main fog pixel shader
float4 main_ps(float2 texcoords : TEXCOORD, float4 positionPS : SV_Position) : SV_Target0
{
   // Sample scene color
   float4 scene_color = scene_texture.Load(int3(positionPS.xy, 0));

   // Sample raw depth
   float raw_depth = depth_texture.Load(int3(positionPS.xy, 0));

   // Sky detection: depth near 1.0 means sky/far plane
   const float sky_threshold = 0.99999;
   bool is_sky = raw_depth > sky_threshold;

   // For sky pixels
   if (is_sky) {
      float fog_factor = 0.0;

      // Disc: trace ray to virtual ground for curved horizon extension
      if (pp_fog_disc_radius > 0.001) {
         float2 ndc;
         ndc.x = texcoords.x * 2.0 - 1.0;
         ndc.y = (1.0 - texcoords.y) * 2.0 - 1.0;

         float3 view_dir;
         view_dir.x = ndc.x / pp_proj_scale_x;
         view_dir.y = ndc.y / pp_proj_scale_y;
         view_dir.z = 1.0;

         float3 world_dir = normalize(mul(float4(view_dir, 0.0), pp_inv_view_matrix).xyz);

         if (world_dir.y < -0.0001) {
            float t = (pp_height_base - pp_camera_position.y) / world_dir.y;
            if (t > 0.0) {
               float3 hit_pos = pp_camera_position + world_dir * t;
               float disc_fade = calculate_disc_fade(hit_pos);
               if (disc_fade > 0.001) {
                  float hit_dist = t;
                  float dist_fog = calculate_distance_fog(hit_dist);
                  float height_fog = calculate_height_fog(hit_pos, hit_dist);
                  fog_factor = saturate(dist_fog + height_fog) * disc_fade;
               }
            }
         }
      }

      // Normal sky fog: use max so disc extends fog but doesn't remove it
      if (pp_apply_to_sky) {
         float linear_depth = linearize_depth(raw_depth);
         float3 world_pos = reconstruct_world_position(texcoords, linear_depth);
         float dist_fog = calculate_distance_fog(linear_depth);
         float height_fog = calculate_height_fog(world_pos, linear_depth);
         float sky_fog = saturate(dist_fog + height_fog);
         fog_factor = max(fog_factor, sky_fog);
      }

      if (fog_factor > 0.001) {
         scene_color.rgb = apply_fog_to_color(scene_color.rgb, fog_factor);
      }
      return scene_color;
   }

   // Normal (non-sky) pixels
   float linear_depth = linearize_depth(raw_depth);
   float distance = linear_depth;
   float3 world_pos = reconstruct_world_position(texcoords, linear_depth);

   float dist_fog = calculate_distance_fog(distance);
   float height_fog = calculate_height_fog(world_pos, distance);
   float fog_factor = saturate(dist_fog + height_fog);

   if (fog_factor < 0.001) {
      return scene_color;
   }

   scene_color.rgb = apply_fog_to_color(scene_color.rgb, fog_factor);

   return scene_color;
}

// Disabled/passthrough variant - just copies scene through
float4 passthrough_ps(float2 texcoords : TEXCOORD, float4 positionPS : SV_Position) : SV_Target0
{
   return scene_texture.Load(int3(positionPS.xy, 0));
}

// Debug shader - visualize linearized depth as grayscale
// Use this to diagnose depth linearization issues
float4 debug_depth_ps(float2 texcoords : TEXCOORD, float4 positionPS : SV_Position) : SV_Target0
{
   float raw_depth = depth_texture.Load(int3(positionPS.xy, 0));

   // Show raw depth in red channel
   // Show linearized depth (normalized to 0-1 range assuming max 1000 units) in green
   // Show linearization params validity in blue

   float linear_depth = linearize_depth(raw_depth);
   float normalized_linear = saturate(linear_depth / 1000.0);  // Normalize to ~1000 unit range

   // Check if params are valid (non-zero)
   float params_valid = (pp_depth_linearize_params.x != 0.0 && pp_depth_linearize_params.y != 0.0) ? 1.0 : 0.0;

   return float4(raw_depth, normalized_linear, params_valid, 1.0);
}

// Debug shader - visualize world Y position
// Red = world Y mapped to 0-1 range (using -400 to 1100 to cover typical SWBF2 worlds)
// Green = height fog factor
// Blue = whether position is within fog height range
float4 debug_world_y_ps(float2 texcoords : TEXCOORD, float4 positionPS : SV_Position) : SV_Target0
{
   float raw_depth = depth_texture.Load(int3(positionPS.xy, 0));

   // Skip sky
   if (raw_depth > 0.99999) {
      return float4(0.5, 0.5, 0.5, 1.0);  // Gray for sky
   }

   float linear_depth = linearize_depth(raw_depth);
   float3 world_pos = reconstruct_world_position(texcoords, linear_depth);

   // Map world Y to visible range: -400 to 1100 -> 0 to 1
   float world_y_normalized = saturate((world_pos.y + 400.0) / 1500.0);

   // Calculate height fog factor for comparison
   float height_fog = calculate_height_fog(world_pos, linear_depth);

   // Check if within height range (pp_height_base to pp_height_ceiling)
   float in_range = (world_pos.y >= pp_height_base && world_pos.y <= pp_height_ceiling) ? 1.0 : 0.0;

   return float4(world_y_normalized, height_fog, in_range, 1.0);
}

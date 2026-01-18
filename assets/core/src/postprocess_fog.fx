// Distance Fog Post-Process Effect
// BF3-faithful atmosphere and fog system
//
// BF3 uses a two-stage approach:
// 1. Atmosphere: cubemap-based sky color blended by distance (pow formula)
// 2. Fog: constant color overlay based on depth
// Both are blended separately for maximum control

Texture2D<float3> scene_input : register(t0);
Texture2D<float> depth_near : register(t1);
Texture2D<float> depth_far : register(t2);
Texture2D<float3> noise_texture : register(t3);
TextureCube<float3> atmosphere_cubemap : register(t4);
TextureCube<float3> space_cubemap : register(t5);

SamplerState linear_wrap_sampler : register(s0);
SamplerState linear_clamp_sampler : register(s1);

cbuffer FogConstants : register(b1)
{
   float4x4 inv_view_proj;         // 64 bytes (near scene)
   float4x4 inv_view_proj_far;     // 64 bytes (far scene - for proper depth linearization)

   float3 fog_color;               // 12 bytes
   float fog_density;              // 4 bytes (BF3: fogcolour.w)

   float3 camera_position;         // 12 bytes
   float fog_start;                // 4 bytes

   float fog_end;                  // 4 bytes
   float height_density;           // 4 bytes
   float height_base;              // 4 bytes (BF3: atmosdata.y)
   float height_falloff;           // 4 bytes

   float3 sun_direction;           // 12 bytes
   float sun_intensity;            // 4 bytes

   float3 sun_color;               // 12 bytes
   float sun_power;                // 4 bytes

   float max_fog_opacity;          // 4 bytes
   float noise_scale;              // 4 bytes
   float noise_intensity;          // 4 bytes
   float cubemap_mip_scale;        // 4 bytes (blur cubemap for close geometry)

   float time;                     // 4 bytes
   int use_atmosphere_cubemap;     // 4 bytes
   float _pad_time0;               // 4 bytes padding
   float _pad_time1;               // 4 bytes padding

   // BF3 atmosdata equivalent
   float atmos_intensity;          // 4 bytes (BF3: atmosdata.x - multiplied by sqrt(dist))
   float atmos_falloff;            // 4 bytes (power for distance curve: 0.5=sqrt, 1.0=linear)
   float horizon_offset;           // 4 bytes (shifts cubemap lookup toward horizon)
   float _pad_atmos;               // 4 bytes padding

   // BF3 height blending: lerp(atmos, atmos_high, heightfactor * height_blend_weight)
   float atmos_high_intensity;     // 4 bytes (BF3: atmosdata.z - atmosphere at high altitude)
   float height_blend_weight;      // 4 bytes (BF3: atmosdata.w - weight for height blending)
   float2 _pad_height;             // 8 bytes padding

   // Cubemap alignment transform (for matching SWBF2 skyboxes)
   float3x3 cubemap_rotation;      // 48 bytes (3 rows of float4 with padding)
   float3 cubemap_scale;           // 12 bytes
   float _pad_scale;               // 4 bytes padding
   float3 cubemap_offset;          // 12 bytes
   float _pad_offset;              // 4 bytes padding

   // Cloud integration (set by clouds system before postprocess)
   float3 cloud_tint;              // 12 bytes
   float cloud_boost;              // 4 bytes

   // Zenith haze
   float zenith_haze;              // 4 bytes
   float _pad_zenith;              // 4 bytes padding

   // BF3-style space cubemap blending (altitude-based)
   float altitude_blend_start;    // 4 bytes - altitude where space starts fading in
   float altitude_blend_end;      // 4 bytes - altitude where space is 100%
   float sky_blend_override;      // 4 bytes - manual override: 0=auto, >0=manual blend
   int use_space_cubemap;         // 4 bytes - enable dual cubemap blending
   float2 _pad_space;             // 8 bytes padding

   // Debug: depth linearization visualization
   int debug_depth_enabled;       // 4 bytes - show debug visualization
   int debug_buffer_mode;         // 4 bytes - 0=min, 1=near only, 2=far only, 3=show which
   float debug_max_distance;      // 4 bytes - max distance for color visualization
   float near_scene_near;         // 4 bytes - near scene near plane
   float near_scene_far;          // 4 bytes - near scene far plane
   float far_scene_near;          // 4 bytes - far scene near plane
   float far_scene_far;           // 4 bytes - far scene far plane
   float _pad_debug;              // 4 bytes padding
}

// =============================================================================
// BF3 ATMOSPHERE CALCULATION
// Exact algorithm from SWBF3 shaders with height blending
// =============================================================================

// BF3's calcatmosphere() function - samples cubemap and computes distance falloff
// Now with cubemap alignment transform for matching SWBF2 skyboxes
// Added: mip-level blur to prevent baked-in sun from showing through close geometry
// Added: BF3-style dual cubemap blending (atmosphere ↔ space) based on camera altitude
float4 bf3_calcatmosphere(float3 viewdir, float density, float falloff, float horizon, float mip_scale, float camera_altitude)
{
   float dist = length(viewdir);
   float3 lookup = viewdir / max(dist, 0.001);

   // Apply cubemap alignment transform (for matching SWBF2 skyboxes)
   // Order: rotate -> scale -> offset -> normalize
   lookup = mul(cubemap_rotation, lookup);  // Apply rotation
   lookup *= cubemap_scale;                  // Apply scale
   lookup += cubemap_offset;                 // Apply offset
   lookup = normalize(lookup);               // Re-normalize after transform

   // BF3: lookup.y -= horizon * (1.0 - lookup.y)
   // Shifts the lookup toward the horizon for more sky influence
   lookup.y -= horizon * (1.0 - lookup.y);

   // Sample atmosphere color from cubemap or use fog color
   float3 atmos_color;
   if (use_atmosphere_cubemap)
   {
      // Mip-level blur: closer geometry samples blurrier mip levels
      // This prevents baked-in sun from bleeding through close objects
      float mip_level = 0.0;
      if (mip_scale > 0.0)
      {
         // Inverse: close = high mip (blurry), far = low mip (sharp)
         float blur_factor = 1.0 - saturate(pow(dist, falloff) * density);
         mip_level = blur_factor * mip_scale * 7.0;  // 7 = typical max mip levels
      }
      atmos_color = atmosphere_cubemap.SampleLevel(linear_clamp_sampler, lookup, mip_level).rgb;

      // BF3-style dual cubemap blending: atmosphere ↔ space based on camera altitude
      // This mirrors BF3's skyblend system where ground and space environments blend at altitude
      if (use_space_cubemap)
      {
         float3 space_color = space_cubemap.SampleLevel(linear_clamp_sampler, lookup, mip_level).rgb;

         // Calculate altitude-based blend factor
         float sky_blend = sky_blend_override;
         if (sky_blend <= 0.0)
         {
            // Auto mode: smoothstep transition between altitude thresholds
            sky_blend = smoothstep(altitude_blend_start, altitude_blend_end, camera_altitude);
            sky_blend *= sky_blend;  // Squared for smoother BF3-style transition
         }

         atmos_color = lerp(atmos_color, space_color, sky_blend);
      }
   }
   else
   {
      atmos_color = fog_color;
   }

   // BF3 formula: pow(dist, falloff) * density
   // falloff=0.5 gives sqrt (BF3 default), 1.0 gives linear, 2.0 gives quadratic
   float atmos_amount = saturate(pow(dist, falloff) * density);

   return float4(atmos_color, atmos_amount);
}

// BF3 vertex shader atmosphere calculation with height blending
// This is the exact algorithm from BF3's vertex shaders
float bf3_calculate_atmosphere_factor(float3 world_pos, float3 view_dir, float dist)
{
   // BF3: dir = worldpos.xyz - worldviewpos
   float3 dir = world_pos - camera_position;

   // BF3: Horizon clamping - clips ray at atmosphere height
   // t = (atmosdata.y - worldviewpos.y) / dir.y
   // dir *= (dir.y>0)? saturate(t) : saturate(1-t)
   if (height_base > 0.0)
   {
      float t = (height_base - camera_position.y) / (dir.y + 0.0001);
      float clamp_factor = (dir.y > 0) ? saturate(t) : saturate(1.0 - t);
      dir *= clamp_factor;
   }

   // BF3: Distance-based atmosphere
   // atmos = sqrt(length(dir)) * atmosdata.x
   float clamped_dist = length(dir);
   float atmos = pow(clamped_dist, atmos_falloff) * atmos_intensity;

   // Fade in atmosphere starting from fog_start distance (prevents bleed-through on close geometry)
   float start_fade = saturate((dist - fog_start) / max(fog_start, 1.0));
   atmos *= start_fade;

   // BF3: Height-based blending between low and high atmosphere
   // heightdiff = saturate(2.0 * dir.y/atmosdata.y - 1.0)
   // atmos = lerp(atmos, atmosdata.z, saturate(heightdiff * atmosdata.w))
   if (height_blend_weight > 0.0 && height_base > 0.0)
   {
      float height_diff = saturate(2.0 * dir.y / height_base - 1.0);
      float blend_factor = saturate(height_diff * height_blend_weight);
      atmos = lerp(atmos, atmos_high_intensity, blend_factor);
   }

   return saturate(atmos);
}

// BF3 depth-based fog intensity calculation
// This is separate from atmosphere - based on screen depth
float bf3_calculate_depth_fog(float depth, float dist)
{
   // BF3: intensity = (wz - depthdata.z) * depthdata.w
   // where wz is the linearized world depth
   // We use our fog_start/fog_end as the depth range
   float normalized_depth = saturate((dist - fog_start) / max(fog_end - fog_start, 1.0));

   // Double smoothstep for soft transition (matching existing behavior)
   normalized_depth = normalized_depth * normalized_depth * (3.0 - 2.0 * normalized_depth);
   normalized_depth = normalized_depth * normalized_depth * (3.0 - 2.0 * normalized_depth);

   return normalized_depth * fog_density;
}

// =============================================================================
// MAIN PIXEL SHADER - BF3 TWO-STAGE FOG
// Stage 1: Atmosphere (cubemap-based sky color, distance falloff with height blend)
// Stage 2: Fog (constant color overlay based on depth)
// =============================================================================

// Simple depth linearization using projection matrix elements
// Formula: distance = m43 / (depth - m33)
float linearize_depth_direct(float raw_depth, float m33, float m43)
{
   float denom = raw_depth - m33;
   if (abs(denom) < 0.0001)
      return 100000.0; // Sky/infinite
   return m43 / denom;
}

float4 fog_main_ps(float2 texcoords : TEXCOORD, float4 positionSS : SV_Position) : SV_Target
{
   float3 scene = scene_input[positionSS.xy];
   float dn = depth_near[positionSS.xy];
   float df = depth_far[positionSS.xy];

   // DEBUG MODE: Use GUI-controlled values for linearization testing
   if (debug_depth_enabled)
   {
      // Compute projection matrix elements from near/far planes
      // Formula: m33 = far / (far - near), m43 = -near * far / (far - near)
      float near_m33 = near_scene_far / (near_scene_far - near_scene_near);
      float near_m43 = -near_scene_near * near_scene_far / (near_scene_far - near_scene_near);

      float far_m33 = far_scene_far / (far_scene_far - far_scene_near);
      float far_m43 = -far_scene_near * far_scene_far / (far_scene_far - far_scene_near);

      // Linearize both depth buffers using GUI values
      float dist_near = linearize_depth_direct(dn, near_m33, near_m43);
      float dist_far = linearize_depth_direct(df, far_m33, far_m43);

      // Buffer mode selection
      float dist = dist_near;  // Default to near
      if (debug_buffer_mode == 1)
      {
         // Near buffer only
         dist = dist_near;
      }
      else if (debug_buffer_mode == 2)
      {
         // Far buffer only
         dist = dist_far;
      }
      else if (debug_buffer_mode == 3)
      {
         // Show which buffer wins: cyan=near, magenta=far
         float t_near = saturate(dist_near / debug_max_distance);
         float t_far = saturate(dist_far / debug_max_distance);
         if (dist_near < dist_far)
            return float4(0.0, t_near, t_near, 1.0);  // Cyan for near
         else
            return float4(t_far, 0.0, t_far, 1.0);    // Magenta for far
      }
      else
      {
         // Min of both (default)
         dist = min(dist_near, dist_far);
      }

      // Visualize: green=near, red=far, based on debug_max_distance
      float t = saturate(dist / debug_max_distance);
      return float4(t, 1.0 - t, 0.0, 1.0);
   }

   // NORMAL MODE: Use scene ranges for linearization
   // These values come from the debug params which can be set to match the sky file:
   // near_scene_far = NearSceneRange end (e.g., 300)
   // far_scene_far = FarSceneRange (e.g., 5000)

   float scene_near = near_scene_far;   // Sky NearSceneRange end value
   float scene_far = far_scene_far;     // Sky FarSceneRange value

   float m33 = scene_far / (scene_far - scene_near);
   float m43 = -scene_near * scene_far / (scene_far - scene_near);

   // Use minimum depth from both buffers (closest geometry)
   float depth = min(dn, df);

   // Linearize to world distance
   float dist = linearize_depth_direct(depth, m33, m43);

   // Compute view direction from screen coordinates (normalized device coords to view ray)
   float4 clip;
   clip.x = texcoords.x * 2.0 - 1.0;
   clip.y = (1.0 - texcoords.y) * 2.0 - 1.0;
   clip.z = 1.0;  // Far plane for direction
   clip.w = 1.0;

   // Get view ray direction from inverse view-projection
   float4 world_far = mul(inv_view_proj, clip);
   float3 view_ray = normalize(world_far.xyz / world_far.w - camera_position);

   // Compute world position from camera + linearized distance along view ray
   float3 world_pos = camera_position + view_ray * dist;

   // Skip sky (very far pixels)
   if (dist > 50000.0)
      return float4(scene, 1.0);

   // View direction for atmosphere sampling
   float3 view_dir = world_pos - camera_position;

   // Stage 1: BF3 Atmosphere (cubemap-based sky color blended by distance)
   float4 atmos = bf3_calcatmosphere(view_dir, atmos_intensity, atmos_falloff,
                                     horizon_offset, cubemap_mip_scale, camera_position.y);

   // Apply BF3 atmosphere calculation with height blending
   float atmos_factor = bf3_calculate_atmosphere_factor(world_pos, view_dir, dist);

   // Stage 2: BF3 Depth Fog (constant color overlay based on depth)
   float fog_factor = bf3_calculate_depth_fog(dn, dist);

   // Cloud integration: boost fog when inside clouds
   fog_factor = saturate(fog_factor + cloud_boost);

   // Combine: atmosphere first, then fog on top
   float3 result = scene;

   // Apply atmosphere (sky color blended by distance)
   result = lerp(result, atmos.rgb * cloud_tint, saturate(atmos_factor));

   // Apply fog (constant color overlay)
   result = lerp(result, fog_color * cloud_tint, saturate(fog_factor * max_fog_opacity));

   return float4(result, 1.0);
}

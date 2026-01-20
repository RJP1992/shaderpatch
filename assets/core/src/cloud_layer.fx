// SWBF3-style cloud layer shader (3-layer system)
// Renders procedural clouds on curved planes using 2D noise with octave blending
// and lightray-stepped volumetric lighting

#pragma warning(disable : 3571)

// Scene depth for occlusion testing
Texture2D<float> depth_texture : register(t0);

// Two 4-channel noise textures (8 octaves total)
Texture2D<float4> noise_tex0 : register(t1);
Texture2D<float4> noise_tex1 : register(t2);

SamplerState wrap_sampler : register(s0);

// Per-layer parameters (160 bytes each)
struct CloudLayerParams
{
   // Lit cloud color + cover (16B)
   float3 light_color;
   float cover;

   // Dark/shadow cloud color + sharpness (16B)
   float3 dark_color;
   float sharpness;

   // Noise octave weights (32B)
   float4 octave_weights_0to3;
   float4 octave_weights_4to7;

   // Noise octave evolution frequencies (32B)
   float4 octave_evol_freqs_0to3;
   float4 octave_evol_freqs_4to7;

   // Geometry (16B)
   float altitude;
   float curved_radius;
   float plane_size;
   float tiling_scale;

   // Animation (16B)
   float wind_speed;
   float wind_angle;
   float half_height;
   float lightray_step;

   // Lighting (16B)
   float max_lighting;
   float min_lighting;
   float scattering;
   uint enabled;

   // World-space plane center (16B)
   float plane_center_x;
   float plane_center_z;
   float _padding0;
   float _padding1;
};

cbuffer CloudLayerConstants : register(b0)
{
   // Inverse view matrix for world position reconstruction (64B)
   float4x4 cl_inv_view_matrix;

   // Camera info (16B)
   float3 cl_camera_position;
   float cl_time;

   // Sun direction + layer count (16B)
   float3 cl_sun_direction;
   uint cl_layer_count;

   // Projection params + depth linearization (16B)
   float cl_proj_scale_x;
   float cl_proj_scale_y;
   float2 cl_depth_linearize_params;

   // 3 cloud layers (480B)
   CloudLayerParams cl_layers[3];
}

// Linearize perspective depth to view-space Z
float linearize_depth(float raw_depth)
{
   return cl_depth_linearize_params.x / (cl_depth_linearize_params.y - raw_depth);
}

// Sample cloud density at a world XZ position using SWBF3 octave noise system
float sample_cloud_density(float2 world_xz, CloudLayerParams layer)
{
   // Compute UV from absolute world position (clouds are fixed in world space)
   // Plane size: layer.plane_size * 2000 units total
   float plane_extent = layer.plane_size * 2000.0;
   float2 base_uv = world_xz / plane_extent;

   // Apply tiling scale
   base_uv *= layer.tiling_scale;

   // Wind scroll
   float angle_rad = layer.wind_angle * 0.01745329; // degrees to radians
   float2 scroll_dir = float2(cos(angle_rad), sin(angle_rad));
   float2 scroll_offset = scroll_dir * layer.wind_speed * cl_time;
   base_uv += scroll_offset;

   // SWBF3 cloud_layer_build noise system:
   // Two 4-channel noise textures, each channel is a different octave.
   // Animated blend between the two textures per-channel using evolution frequencies.
   // Then weighted sum of all 8 channels produces density.

   // Sample both noise textures at the same UV
   float4 tex0_sample = noise_tex0.SampleLevel(wrap_sampler, base_uv, 0);
   float4 tex1_sample = noise_tex1.SampleLevel(wrap_sampler, base_uv, 0);

   // SWBF3 octave evolution: per-channel animated blend between the two textures
   float4 blend_0to3 = 0.5 + 0.5 * sin(cl_time * layer.octave_evol_freqs_0to3);
   float4 blend_4to7 = 0.5 + 0.5 * sin(cl_time * layer.octave_evol_freqs_4to7);

   // Blend between tex0 and tex1 per channel
   float4 blended_0to3 = lerp(tex0_sample, tex1_sample, blend_0to3);

   // For octaves 4-7, sample at a different scale to get more frequency variation
   float2 uv2 = base_uv * 2.17 + float2(0.31, 0.67); // offset to decorrelate
   float4 tex0_sample2 = noise_tex0.SampleLevel(wrap_sampler, uv2, 0);
   float4 tex1_sample2 = noise_tex1.SampleLevel(wrap_sampler, uv2, 0);
   float4 blended_4to7 = lerp(tex0_sample2, tex1_sample2, blend_4to7);

   // SWBF3 cloud_layer_build formula:
   // textmp = lerp(tex1, tex2, blend) - 0.5
   // density = dot(textmp, weights) + 0.5
   float4 centered_0to3 = blended_0to3 - 0.5;
   float4 centered_4to7 = blended_4to7 - 0.5;

   float density = dot(centered_0to3, layer.octave_weights_0to3)
                 + dot(centered_4to7, layer.octave_weights_4to7)
                 + 0.5;

   return density;
}

// Intersect ray with curved cloud plane for a specific layer
// Returns: t > 0 if hit, t <= 0 if no hit
// Curved surface: y = altitude - curvature * ((x - center_x)² + (z - center_z)²)
float intersect_cloud_layer(float3 world_dir, CloudLayerParams layer)
{
   float curvature = (layer.curved_radius > 0.001) ? (1.0 / (2.0 * layer.curved_radius)) : 0.0;

   float t;

   if (curvature > 0.00001) {
      // Offset from plane center (world-fixed)
      float2 plane_center = float2(layer.plane_center_x, layer.plane_center_z);
      float2 offset = cl_camera_position.xz - plane_center;

      // Quadratic intersection: a*t² + b*t + c = 0
      // For surface: y = altitude - K*((x-cx)² + (z-cz)²)
      float horizontal_sq = world_dir.x * world_dir.x + world_dir.z * world_dir.z;
      float offset_dot_dir = offset.x * world_dir.x + offset.y * world_dir.z;
      float offset_sq = dot(offset, offset);

      float a = curvature * horizontal_sq;
      float b = 2.0 * curvature * offset_dot_dir + world_dir.y;
      float c = curvature * offset_sq + cl_camera_position.y - layer.altitude;

      if (abs(a) < 0.000001) {
         if (abs(b) < 0.0001) return -1.0;
         t = -c / b;
      }
      else {
         float discriminant = b * b - 4.0 * a * c;
         if (discriminant < 0.0) return -1.0;

         float sqrt_disc = sqrt(discriminant);
         float t1 = (-b - sqrt_disc) / (2.0 * a);
         float t2 = (-b + sqrt_disc) / (2.0 * a);

         if (t1 > 0.01 && t2 > 0.01)
            t = min(t1, t2);
         else if (t1 > 0.01)
            t = t1;
         else if (t2 > 0.01)
            t = t2;
         else
            return -1.0;
      }
   }
   else {
      // Flat plane intersection
      if (abs(world_dir.y) < 0.0001) return -1.0;
      t = (layer.altitude - cl_camera_position.y) / world_dir.y;
   }

   return t;
}

// Compute cloud color and alpha for a single layer at a given hit position
float4 compute_layer_cloud(float3 hit_pos, float t, CloudLayerParams layer)
{
   // Check if within plane bounds (world-fixed, not camera-relative)
   float plane_extent = layer.plane_size * 1000.0; // half-size
   float2 plane_center = float2(layer.plane_center_x, layer.plane_center_z);
   float2 offset_from_center = hit_pos.xz - plane_center;
   float dist_from_center = length(offset_from_center);
   if (dist_from_center > plane_extent) return float4(0, 0, 0, 0);

   // Edge fade for smooth boundary
   float edge_fade = 1.0 - smoothstep(plane_extent * 0.7, plane_extent, dist_from_center);

   // Sample cloud density at hit position
   float density = sample_cloud_density(hit_pos.xz, layer);

   // Apply cover and sharpness (SWBF3 formula)
   float cloud_shape = smoothstep(0.0, layer.sharpness, density - (1.0 - layer.cover));

   if (cloud_shape < 0.001) return float4(0, 0, 0, 0);

   // Apply edge fade
   cloud_shape *= edge_fade;

   // === Lightray stepping for volumetric lighting ===
   float2 sun_xz = normalize(cl_sun_direction.xz + 0.0001);
   float step_size = layer.lightray_step * layer.plane_size * 200.0;

   float optical_depth = 0.0;
   float2 march_pos = hit_pos.xz;

   // 4 light steps
   [unroll]
   for (int i = 1; i <= 4; i++) {
      march_pos += sun_xz * step_size;
      float step_density = sample_cloud_density(march_pos, layer);
      float step_shape = smoothstep(0.0, layer.sharpness, step_density - (1.0 - layer.cover));
      optical_depth += step_shape;
   }

   // Lighting from accumulated optical depth
   float lighting = exp(-optical_depth * layer.scattering);
   lighting = clamp(lighting, layer.min_lighting, layer.max_lighting);

   // Final cloud color
   float3 cloud_color = lerp(layer.dark_color, layer.light_color, lighting);

   // Edge fade based on distance from plane center (world-fixed)
   float horizon_fade = 1.0 - smoothstep(plane_extent * 0.5, plane_extent * 0.95, dist_from_center);
   float alpha = cloud_shape * horizon_fade;

   // Return premultiplied alpha
   return float4(cloud_color * alpha, alpha);
}

// Fullscreen triangle vertex shader
struct Vs_output
{
   float2 texcoords : TEXCOORD;
   float4 positionPS : SV_Position;
};

Vs_output main_vs(uint id : SV_VertexID)
{
   Vs_output output;

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

// Main cloud layer pixel shader - renders all 3 layers back-to-front
float4 main_ps(float2 texcoords : TEXCOORD, float4 positionPS : SV_Position) : SV_Target0
{
   // Sample depth for occlusion
   float raw_depth = depth_texture.Load(int3(positionPS.xy, 0));
   float scene_dist = 100000.0; // default to far

   // Only compute scene distance for non-sky pixels
   if (raw_depth < 0.99999) {
      scene_dist = linearize_depth(raw_depth);
   }

   // Reconstruct view ray direction
   float2 ndc;
   ndc.x = texcoords.x * 2.0 - 1.0;
   ndc.y = (1.0 - texcoords.y) * 2.0 - 1.0;

   float3 view_dir;
   view_dir.x = ndc.x / cl_proj_scale_x;
   view_dir.y = ndc.y / cl_proj_scale_y;
   view_dir.z = 1.0;

   // Transform view direction to world space
   float3 world_dir = normalize(mul(float4(view_dir, 0.0), cl_inv_view_matrix).xyz);

   // Convert scene depth from view-space Z to world-space ray distance
   // view_dir.z = 1.0, so: view_z = t / length(view_dir), thus: t = view_z * length(view_dir)
   float view_dir_length = length(view_dir);
   float scene_world_dist = scene_dist * view_dir_length;

   // Collect all layer hits with their distances
   struct LayerHit {
      float t;
      int layer_idx;
   };
   LayerHit hits[3];
   int hit_count = 0;

   // Find intersections for all enabled layers
   [unroll]
   for (int i = 0; i < 3; i++) {
      if (cl_layers[i].enabled == 0) continue;

      float t = intersect_cloud_layer(world_dir, cl_layers[i]);

      // Skip if behind camera or behind scene geometry
      if (t < 0.01) continue;
      if (t > scene_world_dist) continue;

      hits[hit_count].t = t;
      hits[hit_count].layer_idx = i;
      hit_count++;
   }

   if (hit_count == 0) discard;

   // Sort hits by distance (back to front for proper alpha blending)
   // Simple bubble sort for 3 elements max
   for (int j = 0; j < hit_count - 1; j++) {
      for (int k = 0; k < hit_count - j - 1; k++) {
         if (hits[k].t < hits[k + 1].t) {
            LayerHit temp = hits[k];
            hits[k] = hits[k + 1];
            hits[k + 1] = temp;
         }
      }
   }

   // Composite layers back-to-front
   float4 result = float4(0, 0, 0, 0);

   for (int l = 0; l < hit_count; l++) {
      int layer_idx = hits[l].layer_idx;
      float t = hits[l].t;

      // Compute hit position
      float3 hit_pos = cl_camera_position + world_dir * t;

      // Get cloud color and alpha for this layer
      float4 layer_result = compute_layer_cloud(hit_pos, t, cl_layers[layer_idx]);

      // Premultiplied alpha over blending: dst = src + dst * (1 - src.a)
      result.rgb = layer_result.rgb + result.rgb * (1.0 - layer_result.a);
      result.a = layer_result.a + result.a * (1.0 - layer_result.a);
   }

   if (result.a < 0.001) discard;

   return result;
}

// BF3-style Procedural Clouds
// Renders 3 cloud layers using planar intersection and RGBA octave noise sampling

// RGBA octave texture - each channel contains different noise octave:
// R = Large cloud shapes (low frequency)
// G = Medium detail (medium frequency)
// B = Fine detail (high frequency)
// A = Edge variation (cellular/worley)
Texture2D cloud_octaves : register(t0);

Texture2D<float> depth_near : register(t1);
Texture2D<float> depth_far : register(t2);
SamplerState wrap_sampler : register(s0);

struct CloudLayer {
   float height;
   float thickness;
   float scale;
   float density;

   float scroll_speed;
   float scroll_angle;
   float fog_boost_max;
   float curvature;

   float cloud_threshold;
   float cloud_softness;
   float sun_color_influence;
   float lighting_wrap;

   float3 color_lit;
   float cloud_brightness;

   float3 color_dark;
   float min_brightness;

   float4 octave_weights;    // BF3-style RGBA channel weights
   float4 octave_blend;      // Blend between primary/secondary sample per channel

   float use_normal_lighting; // 0.0 = BF3 style, 1.0 = normal map
   float3 _pad;
};

cbuffer CloudConstants : register(b0) {
   float4x4 inverse_view_projection;  // 64 bytes

   float3 camera_position;            // 12 bytes
   float time;                        // 4 bytes

   float3 sun_direction;              // 12 bytes
   float horizon_fade_start;          // 4 bytes

   float3 sun_color;                  // 12 bytes
   float horizon_fade_end;            // 4 bytes

   float distance_fade_start;         // 4 bytes
   float distance_fade_end;           // 4 bytes
   float near_fade_start;             // 4 bytes
   float near_fade_end;               // 4 bytes

   float2 curvature_center;           // 8 bytes (world XZ)
   float2 _pad;                       // 8 bytes

   CloudLayer layers[3];              // 384 bytes (128 * 3)
};

// Input from postprocess vertex shader
struct Vs_input {
   float2 texcoords : TEXCOORD;
   float2 positionSS : POSITION;
   float4 positionPS : SV_Position;
};

// Compute normal from noise gradient (BF3-style self-shadow approach)
float3 compute_noise_normal(float2 uv, float4 weights) {
   float2 texel = 1.0 / 512.0;  // Assume 512x512 texture

   // Sample neighbors for gradient
   float4 octL = cloud_octaves.Sample(wrap_sampler, uv - float2(texel.x, 0));
   float4 octR = cloud_octaves.Sample(wrap_sampler, uv + float2(texel.x, 0));
   float4 octD = cloud_octaves.Sample(wrap_sampler, uv - float2(0, texel.y));
   float4 octU = cloud_octaves.Sample(wrap_sampler, uv + float2(0, texel.y));

   // Compute weighted height at each sample
   float hL = dot(octL - 0.5, weights) + 0.5;
   float hR = dot(octR - 0.5, weights) + 0.5;
   float hD = dot(octD - 0.5, weights) + 0.5;
   float hU = dot(octU - 0.5, weights) + 0.5;

   // Gradient-based normal
   float3 normal;
   normal.x = (hL - hR) * 2.0;
   normal.y = 1.0;  // Up
   normal.z = (hD - hU) * 2.0;

   return normalize(normal);
}

// Main pixel shader - renders all 3 cloud layers
float4 main_ps(Vs_input input) : SV_Target0 {
   // Sample depth buffers (use closer of the two)
   float dn = depth_near[input.positionPS.xy];
   float df = depth_far[input.positionPS.xy];
   float depth = min(dn, df);

   // Reconstruct world position from depth (matching fog shader approach)
   float4 clip;
   clip.x = input.texcoords.x * 2.0 - 1.0;
   clip.y = (1.0 - input.texcoords.y) * 2.0 - 1.0;
   clip.z = depth;
   clip.w = 1.0;

   float4 world_pos = mul(inverse_view_projection, clip);
   world_pos /= world_pos.w;

   // Calculate scene depth distance
   float scene_dist = length(world_pos.xyz - camera_position);

   // Now get ray direction for cloud intersection
   float3 ray_dir = normalize(world_pos.xyz - camera_position);

   // Early out for rays that are too horizontal (causes numerical issues)
   float abs_ray_y = abs(ray_dir.y);
   if (abs_ray_y < 0.01)
      discard;

   // Horizon fade (shared across all layers)
   float horizon_fade = smoothstep(horizon_fade_end, horizon_fade_start, abs_ray_y);

   float4 result = float4(0, 0, 0, 0);

   // Maximum intersection distance to prevent numerical issues
   float max_dist = distance_fade_end * 2.0;

   // Render layers - order depends on camera position relative to layers
   [unroll]
   for (int i = 0; i < 3; i++) {
      CloudLayer layer = layers[i];

      // Skip disabled layers (density = 0)
      if (layer.density <= 0.0)
         continue;

      // Intersect ray with cloud surface (plane or curved paraboloid)
      float t;
      if (layer.curvature > 0.0) {
         // Curved surface centered at curvature_center (world XZ):
         // y = height - curvature * ((x - cx)² + (z - cz)²)
         // Solving quadratic: a*t² + b*t + c = 0

         // Camera offset from curvature center
         float dx0 = camera_position.x - curvature_center.x;
         float dz0 = camera_position.z - curvature_center.y;

         float horizontal_sq = ray_dir.x * ray_dir.x + ray_dir.z * ray_dir.z;
         float a = layer.curvature * horizontal_sq;
         float b = ray_dir.y + 2.0 * layer.curvature * (dx0 * ray_dir.x + dz0 * ray_dir.z);
         float c = camera_position.y - layer.height + layer.curvature * (dx0 * dx0 + dz0 * dz0);

         float discriminant = b * b - 4.0 * a * c;
         if (discriminant < 0.0)
            continue;

         // Take the closer positive root
         float sqrt_disc = sqrt(discriminant);
         float t1 = (-b - sqrt_disc) / (2.0 * a);
         float t2 = (-b + sqrt_disc) / (2.0 * a);

         if (t1 > 0.0)
            t = t1;
         else if (t2 > 0.0)
            t = t2;
         else
            continue;
      } else {
         // Flat plane intersection
         float height_diff = layer.height - camera_position.y;
         t = height_diff / ray_dir.y;
      }

      // Skip if plane is behind camera, intersection is too far, or behind scene geometry
      if (t < 0.0 || t > max_dist || t > scene_dist)
         continue;

      float3 hit_pos = camera_position + ray_dir * t;
      float dist = t;  // t is already the distance along the ray

      // Distance fade
      float dist_fade = 1.0 - smoothstep(distance_fade_start, distance_fade_end, dist);
      if (dist_fade < 0.001)
         continue;

      // Near camera fade (smooth transition when flying through clouds)
      float near_fade = smoothstep(near_fade_end, near_fade_start, dist);

      // Sample noise at this layer's scale with wind animation
      float angle_rad = layer.scroll_angle * 0.01745329;  // degrees to radians
      float2 scroll_dir = float2(cos(angle_rad), sin(angle_rad));
      float2 scroll_offset = scroll_dir * layer.scroll_speed * time;

      float2 base_uv = hit_pos.xz * layer.scale;

      // Per-layer offset based on height - breaks repetition between layers
      float2 layer_offset = float2(layer.height * 0.01, layer.height * 0.007);
      base_uv += layer_offset;

      // === BF3-STYLE RGBA OCTAVE SAMPLING ===

      // Primary sample
      float2 uv1 = base_uv + scroll_offset;
      float4 octaves1 = cloud_octaves.Sample(wrap_sampler, uv1);

      // Secondary sample at different scale and offset for more variation
      // Rotated 37 degrees to break grid alignment
      const float rot_sin = 0.6018;  // sin(37°)
      const float rot_cos = 0.7986;  // cos(37°)
      float2 rotated_uv = float2(
         base_uv.x * rot_cos - base_uv.y * rot_sin,
         base_uv.x * rot_sin + base_uv.y * rot_cos
      );
      float2 uv2 = rotated_uv * 2.0 + scroll_offset * 1.3;
      float4 octaves2 = cloud_octaves.Sample(wrap_sampler, uv2);

      // BF3-style per-channel blend between the two samples
      float4 blended_octaves = lerp(octaves1, octaves2, layer.octave_blend);

      // BF3 formula: center values and apply weighted dot product
      float combined = dot(blended_octaves - 0.5, layer.octave_weights) + 0.5;

      // Apply threshold to create cloud shapes
      float raw_density = combined - layer.cloud_threshold;

      // Soft edge using smoothstep
      float shape = smoothstep(0.0, layer.cloud_softness, raw_density);
      float alpha = shape * layer.density * horizon_fade * dist_fade * near_fade;

      if (alpha < 0.001)
         continue;

      // === LIGHTING ===
      float sun_facing;

      if (layer.use_normal_lighting > 0.5) {
         // Normal map style lighting - compute normal from noise gradient
         float3 cloud_normal = compute_noise_normal(uv1, layer.octave_weights);
         float sun_dot = dot(cloud_normal, -sun_direction);
         sun_facing = saturate(sun_dot * (1.0 - layer.lighting_wrap) + layer.lighting_wrap);
      } else {
         // BF3 style - simple noise-based self-shadow
         // Use the blended noise value as a proxy for "thickness above"
         float self_shadow = saturate(combined * 0.8 + 0.2);

         // Simple sun-facing based on view vs sun direction
         float view_sun = dot(-ray_dir, -sun_direction);
         float base_lit = saturate(view_sun * 0.5 + 0.5);

         sun_facing = base_lit * lerp(0.6, 1.0, self_shadow);
         sun_facing = saturate(sun_facing * (1.0 - layer.lighting_wrap) + layer.lighting_wrap);
      }

      // Interpolate between dark and lit colors
      float3 color = lerp(layer.color_dark, layer.color_lit, sun_facing);

      // Apply sun color influence
      color *= lerp(float3(1, 1, 1), sun_color, layer.sun_color_influence);

      // Apply brightness with minimum floor
      color *= max(layer.cloud_brightness, layer.min_brightness);

      // Blend with existing result (standard alpha blend)
      result.rgb = lerp(result.rgb, color, alpha);
      result.a = saturate(result.a + alpha * (1.0 - result.a));
   }

   if (result.a < 0.001)
      discard;

   // Premultiply for blend state (SrcBlend=ONE, DestBlend=INV_SRC_ALPHA)
   return float4(result.rgb * result.a, result.a);
}

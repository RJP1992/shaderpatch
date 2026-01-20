// SWBF3-style volumetric cloud volumes
// Renders billboard cloud impostors scattered within a bounding region
// with 3D noise-based shaping and soft depth blending

#pragma warning(disable : 3571)

// Scene depth for soft blending
Texture2D<float> depth_texture : register(t0);

// 3D noise texture for cloud shape
Texture3D<float> noise_tex : register(t1);

SamplerState wrap_sampler : register(s0);

// Per-frame constants
cbuffer CloudVolumeConstants : register(b0)
{
   // View/projection matrices
   float4x4 cv_view_matrix;
   float4x4 cv_proj_matrix;
   float4x4 cv_view_proj_matrix;

   // Camera info
   float3 cv_camera_position;
   float cv_time;

   // Sun direction
   float3 cv_sun_direction;
   float cv_sharpness;

   // Lighting
   float3 cv_light_color;
   float cv_light_scattering;

   float3 cv_dark_color;
   float cv_max_lighting;

   float cv_min_lighting;
   float cv_noise_influence;
   float cv_noise_tiling;
   float cv_density;

   // Depth fade
   float cv_depth_fade_near;
   float cv_depth_fade_far;
   float cv_edge_softness;
   float cv_evolution_speed;

   // Depth linearization
   float2 cv_depth_linearize_params;
   float2 _padding;
}

// Per-instance data (from instance buffer)
struct InstanceData
{
   float3 position;
   float rotation;
   float3 size;
   float noise_offset;
};

// Vertex input
struct Vs_input
{
   // Quad vertex (0-3)
   uint vertex_id : SV_VertexID;
   // Instance data
   uint instance_id : SV_InstanceID;
};

// Vertex output
struct Vs_output
{
   float4 position : SV_Position;
   float3 world_pos : TEXCOORD0;
   float2 uv : TEXCOORD1;
   float3 local_pos : TEXCOORD2;  // Position within cloud volume [-1,1]
   float3 instance_size : TEXCOORD3;
   float noise_offset : TEXCOORD4;
   float view_depth : TEXCOORD5;
};

// Instance buffer
StructuredBuffer<InstanceData> instance_buffer : register(t2);

// Linearize perspective depth to view-space Z
float linearize_depth(float raw_depth)
{
   return cv_depth_linearize_params.x / (cv_depth_linearize_params.y - raw_depth);
}

// 3D noise sampling with octaves
float sample_cloud_noise(float3 pos, float noise_offset)
{
   float3 sample_pos = pos * cv_noise_tiling + noise_offset;

   // Animate noise
   sample_pos += cv_time * cv_evolution_speed * float3(0.1, 0.05, 0.15);

   // Sample with multiple octaves for detail
   float noise = 0.0;
   float amplitude = 1.0;
   float frequency = 1.0;
   float total_amplitude = 0.0;

   [unroll]
   for (int i = 0; i < 4; i++) {
      noise += noise_tex.SampleLevel(wrap_sampler, sample_pos * frequency, 0).r * amplitude;
      total_amplitude += amplitude;
      amplitude *= 0.5;
      frequency *= 2.0;
   }

   return noise / total_amplitude;
}

// Vertex shader - creates camera-facing billboards
Vs_output main_vs(Vs_input input)
{
   Vs_output output;

   // Get instance data
   InstanceData inst = instance_buffer[input.instance_id];

   // Quad corners in local space [-1, 1]
   float2 corners[4] = {
      float2(-1, -1),
      float2( 1, -1),
      float2(-1,  1),
      float2( 1,  1)
   };
   float2 corner = corners[input.vertex_id];

   // UV coordinates [0, 1]
   output.uv = corner * 0.5 + 0.5;

   // Calculate billboard vectors (camera-facing)
   float3 to_camera = cv_camera_position - inst.position;
   float dist_to_cam = length(to_camera);
   to_camera = to_camera / max(dist_to_cam, 0.001);  // Safe normalize

   float3 world_up = float3(0, 1, 0);
   float3 right = cross(world_up, to_camera);
   float right_len = length(right);

   // Handle case where camera is directly above/below
   if (right_len < 0.001) {
      right = float3(1, 0, 0);
   } else {
      right = right / right_len;
   }

   float3 up = cross(to_camera, right);

   // Apply rotation around view axis
   float cos_rot = cos(inst.rotation);
   float sin_rot = sin(inst.rotation);
   float3 rotated_right = right * cos_rot + up * sin_rot;
   float3 rotated_up = -right * sin_rot + up * cos_rot;

   // Scale by instance size
   float3 offset = rotated_right * corner.x * inst.size.x * 0.5
                 + rotated_up * corner.y * inst.size.y * 0.5;

   // World position
   float3 world_pos = inst.position + offset;
   output.world_pos = world_pos;

   // Local position within cloud volume for noise sampling
   output.local_pos = float3(corner, 0);
   output.instance_size = inst.size;
   output.noise_offset = inst.noise_offset;

   // Transform to clip space (row-vector convention: v * M)
   float4 clip_pos = mul(float4(world_pos, 1.0), cv_view_proj_matrix);
   output.position = clip_pos;

   // View depth for soft blending
   float4 view_pos = mul(float4(world_pos, 1.0), cv_view_matrix);
   output.view_depth = -view_pos.z;  // Positive depth

   return output;
}

// Pixel shader - renders cloud with noise and lighting
float4 main_ps(Vs_output input) : SV_Target0
{
   // Distance from center for circular falloff
   float2 center_dist = input.uv * 2.0 - 1.0;
   float radial_dist = length(center_dist);

   // Discard pixels outside the circle
   if (radial_dist > 1.0) discard;

   // DEBUG: Visualize depth comparison
   // Red = cloud depth, Green = scene depth (both normalized to 0-1 range for viz)
   float raw_depth = depth_texture.Load(int3(input.position.xy, 0));
   float scene_depth = linearize_depth(raw_depth);
   float cloud_depth = input.view_depth;

   // Normalize for visualization (assume max distance ~5000)
   float viz_scene = saturate(scene_depth / 5000.0);
   float viz_cloud = saturate(cloud_depth / 5000.0);

   // Show: Red = cloud is closer (should show), Green = scene is closer (should hide)
   // If we see mostly green on terrain, depth test should work
   // If we see red everywhere, cloud depth is wrong
   return float4(viz_cloud, viz_scene, 0.0, 1.0);

   // Depth test - fade out where cloud intersects geometry
   // Skip depth test for sky pixels
   float depth_fade = 1.0;
   if (raw_depth < 0.99999) {
      // If cloud is behind geometry, discard
      if (input.view_depth > scene_depth) discard;

      // Soft depth fade near geometry
      float depth_diff = scene_depth - input.view_depth;
      depth_fade = saturate(depth_diff / cv_depth_fade_near);
   }

   // Base circular density
   float base_density = 1.0 - radial_dist;

   // Sample 3D noise for cloud shape variation
   float3 noise_pos = float3(input.uv, input.noise_offset);
   float noise = sample_cloud_noise(noise_pos, input.noise_offset);

   // Cloud density with noise influence
   float cloud_density = lerp(base_density, base_density * noise, cv_noise_influence);
   cloud_density = smoothstep(0.0, cv_sharpness, cloud_density * cv_density);

   if (cloud_density < 0.001) discard;

   // Simple spherical normal for lighting
   float z_normal = sqrt(max(0.001, 1.0 - radial_dist * radial_dist));
   float3 normal = normalize(float3(center_dist, z_normal));

   // Lighting calculation
   float ndotl = dot(normal, -cv_sun_direction);
   float light_factor = saturate(ndotl * 0.5 + 0.5);  // Wrap lighting
   light_factor = lerp(cv_min_lighting, cv_max_lighting, light_factor);

   // Final color
   float3 cloud_color = lerp(cv_dark_color, cv_light_color, light_factor);

   // Edge softness
   float edge_fade = smoothstep(0.0, cv_edge_softness, base_density);

   // Final alpha with edge fade and depth fade
   float alpha = cloud_density * edge_fade * depth_fade;

   // Premultiplied alpha output
   return float4(cloud_color * alpha, alpha);
}

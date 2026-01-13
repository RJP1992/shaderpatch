// ===========================================================================
// CLOUDS.FX - BF3-Style Mesh-Based Volumetric Clouds
// Each cloud is rendered as a box mesh, density computed per-vertex
// ===========================================================================

Texture2D<float> depth_texture : register(t0);
Texture3D<float4> noise_texture : register(t1);

SamplerState linear_wrap_sampler : register(s0);
SamplerState point_clamp_sampler : register(s1);

cbuffer CloudConstants : register(b1)
{
   float4x4 view_proj;
   float4x4 world_matrix;           // Per-cloud transform
   
   float3 camera_position;
   float cloud_radius_sq;           // radius * radius
   
   float3 volume_center;            // World space center
   float noise_scale;
   
   float3 volume_inv_scale;         // 1.0 / scale for ellipsoid
   float detail_scale;
   
   float3 light_color;
   float detail_strength;
   
   float3 dark_color;
   float noise_influence;
   
   float3 sun_direction;
   float light_absorption;
   
   float2 wind_offset;
   float transparency_near;
   float transparency_far;
   
   float2 depth_unpack;             // For linearizing depth
   float cloud_alpha_intensity;
   float depth_cutoff_dist;
   
   float4 screen_params;            // xy = size, zw = 1/size
}

struct VS_INPUT
{
   float4 position : POSITION;
};

struct VS_OUTPUT
{
   float3 noise_uvw : TEXCOORD0;
   float3 world_pos : TEXCOORD1;
   float3 cloud_color : TEXCOORD2;
   float2 density_alpha : TEXCOORD3;  // x = density, y = near alpha
   float4 screen_pos : TEXCOORD4;
   float4 position : SV_Position;
};

// BF3's implicit volume density function
float implicit_density(float r2)
{
   if (r2 >= 1.0)
      return 0.0;
   
   float r4 = r2 * r2;
   float r6 = r4 * r2;
   return (-4.0 * r6 + 17.0 * r4 - 22.0 * r2) / 9.0 + 1.0;
}

VS_OUTPUT cloud_vs(VS_INPUT input)
{
   VS_OUTPUT output;
   
   // Transform vertex to world space
   float4 world_pos = mul(world_matrix, float4(input.position.xyz, 1.0));
   output.world_pos = world_pos.xyz;
   
   // Transform to clip space
   float4 clip_pos = mul(view_proj, world_pos);
   output.position = clip_pos;
   output.screen_pos = clip_pos;
   
   // Calculate near/far transparency fade (BF3 style)
   float z = clip_pos.w;
   float near = transparency_near;
   float far = transparency_far;
   float range = far - near;
   z = clamp(z, near, far);
   
   // Smooth hermite fade
   float t = (z - near) / range;
   output.density_alpha.y = t * t * (3.0 - 2.0 * t);
   
   // Calculate position relative to volume center (normalized to ellipsoid)
   float3 pos_to_center = (output.world_pos - volume_center) * volume_inv_scale;
   float r2 = dot(pos_to_center, pos_to_center) / cloud_radius_sq;
   
   // BF3 implicit density
   output.density_alpha.x = implicit_density(r2);
   
   // Calculate noise UVs from world position
   output.noise_uvw = output.world_pos * noise_scale + float3(wind_offset.x, 0, wind_offset.y);
   
   // Calculate lighting (simplified from BF3's analytical integration)
   // BF3 computes ray distance through cloud analytically - we'll approximate
   if (output.density_alpha.x > 0.0)
   {
      // Approximate light penetration based on height in cloud
      float height_in_cloud = pos_to_center.y / sqrt(cloud_radius_sq);
      float light_depth = saturate(0.5 + height_in_cloud * 0.5);
      
      // Simple approximation of BF3's analytical light integration
      float light_transmittance = exp(-light_depth * light_absorption * 2.0);
      
      output.cloud_color = lerp(dark_color, light_color, light_transmittance);
   }
   else
   {
      output.cloud_color = light_color;
   }
   
   return output;
}

struct PS_OUTPUT
{
   float4 color : SV_Target0;
};

PS_OUTPUT clouds_ps(VS_OUTPUT input)
{
   PS_OUTPUT output;
   
   // Get screen UV
   float2 screen_uv = input.screen_pos.xy / input.screen_pos.w;
   screen_uv = screen_uv * 0.5 + 0.5;
   screen_uv.y = 1.0 - screen_uv.y;
   
   // Sample scene depth
   float scene_depth = depth_texture.SampleLevel(point_clamp_sampler, screen_uv, 0);
   
   // Linearize depths for comparison
   float scene_z = depth_unpack.y / (scene_depth - depth_unpack.x);
   float cloud_z = input.screen_pos.w;
   
   // Depth cutoff - fade cloud where it intersects geometry
   float depth_diff = scene_z - cloud_z;
   float depth_cutoff = 1.0 - saturate(depth_diff / depth_cutoff_dist);
   depth_cutoff = max(0, depth_cutoff);
   depth_cutoff = depth_cutoff * depth_cutoff;
   
   // Sample 3D noise
   float4 noise_sample = noise_texture.Sample(linear_wrap_sampler, input.noise_uvw);
   float4 detail_sample = noise_texture.Sample(linear_wrap_sampler, input.noise_uvw * (detail_scale / noise_scale));
   
   // Combine noise octaves (BF3 style - weighted sum)
   float noise = noise_sample.r * 0.5 + noise_sample.g * 0.25 + noise_sample.b * 0.125 + noise_sample.a * 0.0625;
   noise = noise * 2.0 - 1.0;  // Remap to -1 to 1
   noise += (detail_sample.r * 2.0 - 1.0) * detail_strength;
   noise = min(noise, 1.0);
   
   // Combine density with noise
   float density = input.density_alpha.x * input.density_alpha.y;
   float alpha = lerp(1.0, noise, noise_influence) - (1.0 - density);
   alpha *= cloud_alpha_intensity;
   
   // Apply depth cutoff
   alpha = max(0, alpha - depth_cutoff);
   
   // Premultiplied alpha output (BF3 style)
   float3 cloud_color = input.cloud_color * alpha;
   
   output.color = float4(cloud_color, alpha);
   
   return output;
}

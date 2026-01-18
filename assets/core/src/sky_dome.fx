// Sky Dome Atmosphere Effect
// BF3-style atmosphere ring visible from space
// Renders as fullscreen effect, only visible at high altitudes

TextureCube<float3> atmosphere_cubemap : register(t0);
Texture2D<float> depth_near : register(t1);
Texture2D<float> depth_far : register(t2);

SamplerState linear_clamp_sampler : register(s0);

cbuffer SkyDomeConstants : register(b1)
{
   float4x4 inv_view_proj;           // 64 bytes

   float3 camera_position;           // 12 bytes
   float atmosphere_density;         // 4 bytes

   float horizon_shift;              // 4 bytes
   float horizon_start;              // 4 bytes
   float horizon_blend;              // 4 bytes
   float fade_start_height;          // 4 bytes

   float fade_end_height;            // 4 bytes
   float3 tint;                      // 12 bytes

   // Cubemap alignment transform
   float3x3 cubemap_rotation;        // 48 bytes (3 rows of float4 with padding)
   float3 cubemap_scale;             // 12 bytes
   float _pad_scale;                 // 4 bytes padding
   float3 cubemap_offset;            // 12 bytes
   float _pad_offset;                // 4 bytes padding
}

// Input from postprocess vertex shader
struct Vs_input
{
   float2 texcoords : TEXCOORD;
   float2 positionSS : POSITION;
   float4 positionPS : SV_Position;
};

float4 main_ps(Vs_input input) : SV_Target0
{
   // Height-based visibility (fade in as camera rises)
   float height_factor = saturate((camera_position.y - fade_start_height)
                                   / max(fade_end_height - fade_start_height, 1.0));

   // Early out if camera is too low
   if (height_factor <= 0.001)
      discard;

   // Sample depth to check for sky
   float dn = depth_near[input.positionPS.xy];
   float df = depth_far[input.positionPS.xy];
   float depth = min(dn, df);

   // Only render on sky pixels (depth at far plane)
   if (depth < 0.9999)
      discard;

   // Get view direction from screen coords
   float4 clip;
   clip.x = input.texcoords.x * 2.0 - 1.0;
   clip.y = (1.0 - input.texcoords.y) * 2.0 - 1.0;
   clip.z = 1.0;  // Far plane
   clip.w = 1.0;

   float4 world_dir = mul(inv_view_proj, clip);
   float3 view_dir = normalize(world_dir.xyz / world_dir.w - camera_position);

   // Apply cubemap transform
   float3 lookup = view_dir;
   lookup = mul(cubemap_rotation, lookup);
   lookup *= cubemap_scale;
   lookup += cubemap_offset;
   lookup = normalize(lookup);

   // BF3 horizon shift - pushes lookup toward horizon
   lookup.y -= horizon_shift * (1.0 - lookup.y);

   // Sample atmosphere cubemap
   float3 atmos_color = atmosphere_cubemap.SampleLevel(linear_clamp_sampler, lookup, 0).rgb;
   atmos_color *= tint;

   // Horizon falloff (BF3 style)
   // Creates a ring of atmosphere concentrated at the horizon
   float vertical = abs(view_dir.y);
   float fadeoff = smoothstep(horizon_start, horizon_shift, vertical);
   fadeoff *= fadeoff;  // Sharper transition

   // Blend between sharp ring and full coverage
   float atmos_alpha = lerp(fadeoff * fadeoff, 1.0, horizon_blend);
   atmos_alpha *= atmosphere_density * height_factor;

   return float4(atmos_color * atmos_alpha, atmos_alpha);
}

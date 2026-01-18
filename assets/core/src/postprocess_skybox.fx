// Skybox Override Effect
// Replaces vanilla skybox with post-process cubemap rendering
// Uses BF3-style atmosphere blending algorithms

TextureCube<float3> ground_cubemap : register(t0);   // Main sky (ground level view)
TextureCube<float3> sky_cubemap : register(t1);      // Atmosphere/space for blending
Texture2D<float> depth_near : register(t2);
Texture2D<float> depth_far : register(t3);
// X24_TYPELESS_G8_UINT format: stencil is in the .y (G) component
Texture2D<uint2> stencil_near : register(t4);
Texture2D<uint2> stencil_far : register(t5);

SamplerState linear_clamp_sampler : register(s0);

cbuffer SkyboxOverrideConstants : register(b1)
{
   float4x4 inv_view_proj;           // 64 bytes

   float3 camera_position;           // 12 bytes
   float sky_distance_threshold;     // 4 bytes

   // Cubemap alignment transform (shared)
   float4 cubemap_rotation_row0;     // 16 bytes
   float4 cubemap_rotation_row1;     // 16 bytes
   float4 cubemap_rotation_row2;     // 16 bytes
   float3 cubemap_scale;             // 12 bytes
   float proj_from_view_m33;         // 4 bytes - for DOF-style depth conversion
   float3 cubemap_offset;            // 12 bytes
   float proj_from_view_m43;         // 4 bytes - for DOF-style depth conversion

   // Atmosphere params (BF3 style)
   float atmos_density;              // 4 bytes
   float horizon_shift;              // 4 bytes
   float horizon_start;              // 4 bytes
   float horizon_blend;              // 4 bytes

   float3 tint;                      // 12 bytes
   float use_atmosphere;             // 4 bytes (bool as float for HLSL compat)

   float debug_mode;                 // 4 bytes - 0=off, 1=show depth, 2=show distance
   float3 _pad;                      // 12 bytes padding
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
   // Sample depth from both buffers and use closer depth
   float dn = depth_near[input.positionPS.xy];
   float df = depth_far[input.positionPS.xy];
   float depth = min(dn, df);

   // Sample stencil from both buffers using Load (X24_TYPELESS_G8_UINT: stencil in .y)
   uint sn = stencil_near.Load(int3(input.positionPS.xy, 0)).y;
   uint sf = stencil_far.Load(int3(input.positionPS.xy, 0)).y;

   // DOF-style distance calculation
   float camera_distance = proj_from_view_m43 / (depth - proj_from_view_m33);

   // Debug mode: visualize depth/distance/stencil values for all pixels
   if (debug_mode > 0.5)
   {
      if (debug_mode < 1.5)
      {
         // Mode 1: Show depth - amplify differences near 1.0
         // depth of 0.999 = black, 1.0 = white
         float vis = saturate((depth - 0.999) * 1000.0);
         return float4(vis, vis, vis, 1.0);
      }
      else if (debug_mode < 2.5)
      {
         // Mode 2: Show distance (log scale)
         // Red = close (100), Yellow = mid (1000), Green = far (10000), Cyan = very far (100000+)
         float log_dist = log10(max(camera_distance, 1.0));
         float r = saturate(1.0 - (log_dist - 2.0));  // < 100
         float g = saturate(1.0 - abs(log_dist - 3.5));  // ~3000
         float b = saturate((log_dist - 4.0) * 0.5);  // > 10000
         return float4(r, g, b, 1.0);
      }
      else if (debug_mode < 3.5)
      {
         // Mode 3: Show near stencil with color coding
         uint stencil = sn;
         float3 c = float3(0, 0, 0);
         if      (stencil == 0) c = float3(0.0, 0.0, 0.0);  // Black
         else if (stencil == 1) c = float3(1.0, 0.0, 0.0);  // Red
         else if (stencil == 2) c = float3(0.0, 1.0, 0.0);  // Green
         else if (stencil == 3) c = float3(0.0, 0.0, 1.0);  // Blue
         else if (stencil == 4) c = float3(1.0, 1.0, 0.0);  // Yellow
         else if (stencil == 5) c = float3(1.0, 0.0, 1.0);  // Magenta
         else if (stencil == 6) c = float3(0.0, 1.0, 1.0);  // Cyan
         else                   c = float3(1.0, 1.0, 1.0);  // White (7+)
         return float4(c, 1.0);
      }
      else if (debug_mode < 4.5)
      {
         // Mode 4: Show far stencil with color coding
         uint stencil = sf;
         float3 c = float3(0, 0, 0);
         if      (stencil == 0) c = float3(0.0, 0.0, 0.0);  // Black
         else if (stencil == 1) c = float3(1.0, 0.0, 0.0);  // Red
         else if (stencil == 2) c = float3(0.0, 1.0, 0.0);  // Green
         else if (stencil == 3) c = float3(0.0, 0.0, 1.0);  // Blue
         else if (stencil == 4) c = float3(1.0, 1.0, 0.0);  // Yellow
         else if (stencil == 5) c = float3(1.0, 0.0, 1.0);  // Magenta
         else if (stencil == 6) c = float3(0.0, 1.0, 1.0);  // Cyan
         else                   c = float3(1.0, 1.0, 1.0);  // White (7+)
         return float4(c, 1.0);
      }
      else
      {
         // Mode 5: Raw stencil components - R=.x (near), G=.y (near), B=.x (far)
         // This helps debug which component has actual stencil data
         uint2 raw_near = stencil_near.Load(int3(input.positionPS.xy, 0));
         uint2 raw_far = stencil_far.Load(int3(input.positionPS.xy, 0));
         return float4(raw_near.x / 255.0, raw_near.y / 255.0, raw_far.x / 255.0, 1.0);
      }
   }

   // Sky detection: require BOTH conditions to pass
   if (depth < 0.9999)
      discard;

   if (camera_distance > 0 && camera_distance < sky_distance_threshold)
      discard;

   // Get view direction from screen coords (using far plane for direction)
   float4 clip;
   clip.x = input.texcoords.x * 2.0 - 1.0;
   clip.y = (1.0 - input.texcoords.y) * 2.0 - 1.0;
   clip.z = 1.0;  // Far plane for direction
   clip.w = 1.0;

   float4 world_dir = mul(inv_view_proj, clip);
   float3 view_dir = normalize(world_dir.xyz / world_dir.w - camera_position);

   // Build rotation matrix from rows
   float3x3 cubemap_rotation = float3x3(
      cubemap_rotation_row0.xyz,
      cubemap_rotation_row1.xyz,
      cubemap_rotation_row2.xyz
   );

   // Apply cubemap transform for alignment
   float3 lookup = view_dir;
   lookup = mul(cubemap_rotation, lookup);
   lookup = normalize(lookup * cubemap_scale + cubemap_offset);

   // Sample ground cubemap (primary sky)
   float3 color = ground_cubemap.SampleLevel(linear_clamp_sampler, lookup, 0).rgb;

   // Apply atmosphere blending if sky cubemap is provided
   if (use_atmosphere > 0.5)
   {
      // BF3 horizon warp for sky/atmosphere lookup
      float3 sky_lookup = lookup;
      sky_lookup.y -= horizon_shift * (1.0 - sky_lookup.y);
      sky_lookup = normalize(sky_lookup);

      // Sample sky/atmosphere cubemap
      float3 atmos_color = sky_cubemap.SampleLevel(linear_clamp_sampler, sky_lookup, 0).rgb;

      // BF3 atmospheric fade algorithm
      // saturate(181.02f * density) gives 1.0 at density ~0.0055
      float sky_atmos = saturate(181.02f * atmos_density);

      // Horizon falloff (smoothstep creates gradient)
      float vertical = abs(view_dir.y);
      float fadeoff = smoothstep(horizon_start, horizon_shift, vertical);
      fadeoff *= fadeoff;  // Square for sharper transition

      // Blend between sharp ring and full coverage
      float atmos = lerp(fadeoff * fadeoff, 1.0, horizon_blend) * sky_atmos;

      // Mix ground and sky/atmosphere
      color = lerp(color, atmos_color, atmos);
   }

   // Apply tint
   color *= tint;

   return float4(color, 1.0);
}

// Early render output - color + depth
struct Early_output
{
   float4 color : SV_Target0;
   float depth : SV_Depth;
};

// Early render pixel shader - renders BEFORE terrain, writes depth=1.0
// Terrain will naturally overwrite these pixels (terrain depth < 1.0)
Early_output early_ps(Vs_input input)
{
   Early_output output;

   // Get view direction from screen coords (using far plane for direction)
   float4 clip;
   clip.x = input.texcoords.x * 2.0 - 1.0;
   clip.y = (1.0 - input.texcoords.y) * 2.0 - 1.0;
   clip.z = 1.0;  // Far plane for direction
   clip.w = 1.0;

   float4 world_dir = mul(inv_view_proj, clip);
   float3 view_dir = normalize(world_dir.xyz / world_dir.w - camera_position);

   // Build rotation matrix from rows
   float3x3 cubemap_rotation = float3x3(
      cubemap_rotation_row0.xyz,
      cubemap_rotation_row1.xyz,
      cubemap_rotation_row2.xyz
   );

   // Apply cubemap transform for alignment
   float3 lookup = view_dir;
   lookup = mul(cubemap_rotation, lookup);
   lookup = normalize(lookup * cubemap_scale + cubemap_offset);

   // Sample ground cubemap
   float3 color = ground_cubemap.SampleLevel(linear_clamp_sampler, lookup, 0).rgb;

   // Apply tint
   color *= tint;

   output.color = float4(color, 1.0);
   output.depth = 1.0;  // Far plane - terrain (with depth < 1.0) will overwrite

   return output;
}

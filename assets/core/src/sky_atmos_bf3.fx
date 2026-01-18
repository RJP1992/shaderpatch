// sky_atmos_bf3.fx - BF3-style atmospheric sky rendering
// Mesh-based atmospheric scattering with horizon warp
// Port of BF3's sky_atmos.hlsl

#include "constants_list.hlsl"
#include "generic_vertex_input.hlsl"
#include "pixel_sampler_states.hlsl"
#include "vertex_transformer.hlsl"

// clang-format off

TextureCube<float3> sky_envmap : register(ps, t7);
TextureCube<float3> atmos_cubemap : register(ps, t8);

cbuffer SkyAtmosConstants : register(MATERIAL_CB_INDEX)
{
   // Atmosphere parameters (BF3 atmoshorizon equivalent)
   float atmos_density;      // BF3: saturate(181.02f * atmosdensity)
   float horizon_shift;      // atmoshorizon[0] - push lookup toward horizon
   float horizon_start;      // atmoshorizon[1] - where fade begins
   float horizon_blend;      // atmoshorizon[2] - 0=sharp ring, 1=full coverage

   // Cubemap alignment transform (shared with fog system)
   float4 cubemap_rotation_row0;
   float4 cubemap_rotation_row1;
   float4 cubemap_rotation_row2;

   float3 cubemap_scale;
   float _pad0;

   float3 cubemap_offset;
   float _pad1;

   float3 tint;
   float _pad2;
};

struct Vs_output
{
   float3 normal_dir : TEXCOORD0;
   float4 vertex_color : COLOR;
   float4 positionPS : SV_Position;
};

Vs_output main_vs(Vertex_input input)
{
   Vs_output output;

   Transformer transformer = create_transformer(input);

   // BF3: normalize position as view direction
   float3 pos = normalize(transformer.positionOS());

   // BF3 X-flip convention
   pos.x = -pos.x;

   output.normal_dir = pos;
   output.vertex_color = get_material_color(input.color());
   output.positionPS = transformer.positionPS();

   return output;
}

float4 main_ps(Vs_output input) : SV_Target0
{
   float3 ndir = normalize(input.normal_dir);

   // Build rotation matrix from rows
   float3x3 cubemap_rotation = float3x3(
      cubemap_rotation_row0.xyz,
      cubemap_rotation_row1.xyz,
      cubemap_rotation_row2.xyz
   );

   // Apply cubemap transform for alignment
   float3 lookup = ndir;
   lookup = mul(cubemap_rotation, lookup);
   lookup *= cubemap_scale;
   lookup += cubemap_offset;
   lookup = normalize(lookup);

   // Sample base sky envmap
   float3 color = sky_envmap.Sample(linear_wrap_sampler, lookup);

   // BF3 Horizon warp - push atmosphere lookup toward horizon
   // This concentrates the atmosphere effect at the horizon
   float3 atmos_lookup = lookup;
   atmos_lookup.y -= horizon_shift * (1.0 - atmos_lookup.y);
   atmos_lookup = normalize(atmos_lookup);

   // Sample atmosphere cubemap
   float3 atmoscolor = atmos_cubemap.Sample(linear_wrap_sampler, atmos_lookup);
   atmoscolor *= tint;

   // BF3 Atmospheric fade calculation
   // Original: skyatmos = saturate(181.02f * atmosdensity)
   float skyatmos = saturate(181.02f * atmos_density);

   // Horizon falloff using smoothstep
   // Original: fadeoff = smoothstep(atmoshorizon[1], atmoshorizon[0], ndir.y)
   float fadeoff = smoothstep(horizon_start, horizon_shift, ndir.y);
   fadeoff *= fadeoff;  // Square for sharper transition

   // Blend between sharp ring (fadeoff^2) and full coverage (1.0)
   // Original: atmos = lerp(fadeoff * fadeoff, 1.0, atmoshorizon[2])
   float atmos = lerp(fadeoff * fadeoff, 1.0, horizon_blend);
   atmos *= skyatmos;

   // Blend atmosphere into sky color
   color = lerp(color, atmoscolor, atmos);

   // Apply vertex color modulation
   color *= input.vertex_color.rgb;

   // Apply lighting scale for HDR
   color *= lighting_scale;

   return float4(color, 1.0);
}

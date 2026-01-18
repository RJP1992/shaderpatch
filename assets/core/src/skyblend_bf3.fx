// skyblend_bf3.fx - BF3-style ground/space sky transition
// Blends between two cubemaps based on external blend factor
// Port of BF3's skyblend.hlsl

#include "constants_list.hlsl"
#include "generic_vertex_input.hlsl"
#include "pixel_sampler_states.hlsl"
#include "vertex_transformer.hlsl"

// clang-format off

// Ground sky cubemap (can be RGBE encoded for HDR)
TextureCube<float4> ground_envmap : register(ps, t7);

// Space sky cubemap (can be RGBE encoded for HDR)
TextureCube<float4> space_envmap : register(ps, t8);

cbuffer SkyBlendConstants : register(MATERIAL_CB_INDEX)
{
   // Blend factor: 0=ground, 1=space (controlled by C++ from camera height)
   float sky_blend;

   // Ambient color multiplier
   float3 sky_ambient_color;

   // Visual options
   int monochrome;          // Desaturation flag
   int use_rgbe;            // Whether cubemaps use RGBE encoding
   float2 _pad0;

   // Cubemap alignment transform
   float4 cubemap_rotation_row0;
   float4 cubemap_rotation_row1;
   float4 cubemap_rotation_row2;

   float3 cubemap_scale;
   float _pad1;

   float3 cubemap_offset;
   float _pad2;
};

// RGBE decoding for HDR cubemaps
// BF3 original: rgb * exp2(round(a * 255.0) - 128.0)
float3 decode_rgbe(float4 rgbe)
{
   float exponent = rgbe.a * 255.0 - 128.0;
   return rgbe.rgb * pow(2.0, exponent);
}

struct Vs_output
{
   float3 cubemap_coords : TEXCOORD0;
   float4 vertex_color : COLOR;
   float4 positionPS : SV_Position;
};

Vs_output main_vs(Vertex_input input)
{
   Vs_output output;

   Transformer transformer = create_transformer(input);

   // BF3: use position directly as cubemap lookup (skyblend doesn't normalize in VS)
   float3 pos = transformer.positionOS();

   output.cubemap_coords = pos;
   output.vertex_color = get_material_color(input.color());
   output.positionPS = transformer.positionPS();

   return output;
}

float4 main_ps(Vs_output input) : SV_Target0
{
   float3 lookup = normalize(input.cubemap_coords);

   // Build rotation matrix from rows
   float3x3 cubemap_rotation = float3x3(
      cubemap_rotation_row0.xyz,
      cubemap_rotation_row1.xyz,
      cubemap_rotation_row2.xyz
   );

   // Apply cubemap transform for alignment
   lookup = mul(cubemap_rotation, lookup);
   lookup *= cubemap_scale;
   lookup += cubemap_offset;
   lookup = normalize(lookup);

   // Sample both cubemaps
   float4 ground_sample = ground_envmap.Sample(linear_wrap_sampler, lookup);
   float4 space_sample = space_envmap.Sample(linear_wrap_sampler, lookup);

   // Decode to HDR if using RGBE encoding
   float3 ground_col, space_col;
   if (use_rgbe) {
      ground_col = decode_rgbe(ground_sample);
      space_col = decode_rgbe(space_sample);
   } else {
      ground_col = ground_sample.rgb;
      space_col = space_sample.rgb;
   }

   // BF3 linear blend: ground + (space - ground) * skyblend
   float3 color = ground_col + (space_col - ground_col) * sky_blend;

   // Optional monochrome desaturation
   if (monochrome) {
      float luma = dot(color, float3(0.299, 0.587, 0.114));
      color = float3(luma, luma, luma);
   }

   // Apply ambient color multiplier
   color *= sky_ambient_color;

   // Apply vertex color modulation
   color *= input.vertex_color.rgb;

   // Apply lighting scale for HDR
   color *= lighting_scale;

   return float4(color, 1.0);
}

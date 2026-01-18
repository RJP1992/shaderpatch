// sky_bf3.fx - BF3-style basic sky rendering
// Renders on game's sky mesh geometry with cubemap
// Port of BF3's sky.hlsl

#include "constants_list.hlsl"
#include "generic_vertex_input.hlsl"
#include "pixel_sampler_states.hlsl"
#include "vertex_transformer.hlsl"

// clang-format off

TextureCube<float3> sky_envmap : register(ps, t7);

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

   // BF3: normalize position as cubemap lookup direction
   float3 pos = normalize(transformer.positionOS());

   // BF3 X-flip convention for cubemap lookup
   pos.x = -pos.x;

   output.cubemap_coords = pos;
   output.vertex_color = get_material_color(input.color());
   output.positionPS = transformer.positionPS();

   return output;
}

float4 main_ps(Vs_output input) : SV_Target0
{
   float3 color = sky_envmap.Sample(linear_wrap_sampler, input.cubemap_coords);

   // Apply vertex color modulation (BF3 multiplies by trans.col)
   color *= input.vertex_color.rgb;

   // Apply lighting scale for HDR
   color *= lighting_scale;

   return float4(color, 1.0);
}

#include "adaptive_oit.hlsl"
#include "constants_list.hlsl"
#include "generic_vertex_input.hlsl"
#include "pixel_sampler_states.hlsl"
#include "pixel_utilities.hlsl"
#include "vertex_transformer.hlsl"
#include "vertex_utilities.hlsl"

// clang-format off

// Textures
Texture2D<float4> texture_a : register(ps, t7);
Texture2D<float4> texture_b : register(ps, t8);

// Game Constants
const static float4 x_texcoords_transform = custom_constants[1];
const static float4 y_texcoords_transform = custom_constants[2];

// Note: Material constants (blend_bottom, blend_top) are no longer used.
// Altitude thresholds are now read from global PSDrawConstants (altitude_blend_start, altitude_blend_end)
// which are set from the Environment tab in Shader Patch. This ensures the visual skybox
// and fog cubemap sampling blend at exactly the same altitudes.

struct Vs_output
{
   float2 texcoords : TEXCOORDS;

   float4 color : COLOR;

   float4 positionPS : SV_Position;
};

Vs_output main_vs(Vertex_input input)
{
   Vs_output output;

   Transformer transformer = create_transformer(input);

   const float3 positionWS = transformer.positionWS();
   const float4 positionPS = transformer.positionPS();

   output.positionPS = positionPS;
   output.texcoords = transformer.texcoords(x_texcoords_transform, y_texcoords_transform);
   output.color = get_material_color(input.color());

   return output;
}

float4 main_ps(Vs_output input) : SV_Target0
{
   // Sample both textures
   float4 color_a = texture_a.Sample(aniso_wrap_sampler, input.texcoords);
   float4 color_b = texture_b.Sample(aniso_wrap_sampler, input.texcoords);

   // Get camera height
   float y = view_positionWS.y;

   // Calculate blend factor (0 = A, 1 = B) using global altitude thresholds
   // These are shared with fog cubemap blending for synchronized transitions
   float blend = saturate((y - altitude_blend_start) / (altitude_blend_end - altitude_blend_start + 0.001));
   blend = smoothstep(0.0, 1.0, blend);

   // Blend textures
   float4 color = lerp(color_a, color_b, blend);

   color *= input.color;
   color.rgb *= lighting_scale;

   return color;
}

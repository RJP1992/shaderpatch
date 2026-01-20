
#include "adaptive_oit.hlsl"
#include "fog_common.hlsl"

#pragma warning(disable : 4000)

// Screen dimensions for UV calculation (set from constant buffer)
cbuffer OITResolveConstants : register(b5)
{
   float2 screen_size;
   uint pp_fog_enabled;
   float _oit_padding;
}

struct VS_OUTPUT
{
   float4 position : SV_Position;
   float2 texcoord : TEXCOORD0;
};

VS_OUTPUT main_vs(uint id : SV_VertexID)
{
   VS_OUTPUT output;

   if (id == 0) {
      output.position = float4(-1.f, -1.f, 0.0, 1.0);
      output.texcoord = float2(0.0, 1.0);
   }
   else if (id == 1) {
      output.position = float4(-1.f, 3.f, 0.0, 1.0);
      output.texcoord = float2(0.0, -1.0);
   }
   else {
      output.position = float4(3.f, -1.f, 0.0, 1.0);
      output.texcoord = float2(2.0, 1.0);
   }

   return output;
}

float4 main_ps(VS_OUTPUT input) : SV_Target
{
   const uint2 index = (uint2)input.position.xy;
   const bool clear = aoit::clear_surface_load_srv(index);

   [branch]
   if (clear) {
      discard;
      return float4(0.0, 0.0, 0.0, 1.0);
   }

   const aoit::Nodes nodes = aoit::load_nodes_srv(index);

   float3 color = float3(0.0, 0.0, 0.0);
   float transmittance = 1.0;

   // Standard OIT accumulation (no per-layer fog - that breaks premultiplied math)
   [unroll]
   for (uint i = 0; i < aoit::node_count; ++i) {
      color += (nodes.color[i] * transmittance);
      transmittance = nodes.transmittance[i];
   }

   // Apply fog once to the final result using front-most depth
   [branch]
   if (pp_fog_enabled && nodes.depth[0] < aoit::empty_node_depth) {
      // Calculate fog factor from front-most transparent surface
      float raw_depth = nodes.depth[0];
      if (raw_depth < 0.99999) {
         float linear_depth = linearize_depth(raw_depth);
         float3 world_pos = reconstruct_world_position(input.texcoord, linear_depth);

         float dist_fog = calculate_distance_fog(linear_depth);
         float height_fog = calculate_height_fog(world_pos, linear_depth);
         float fog_factor = saturate(dist_fog + height_fog);

         if (fog_factor > 0.001) {
            // For premultiplied alpha, we need to add fog contribution scaled by opacity
            // The 'color' here is the premultiplied transparent contribution
            // We blend fog into it based on how much the transparent layers block
            float opacity = 1.0 - transmittance;  // How opaque the transparent stack is
            color = lerp(color, pp_fog_color.rgb * opacity, fog_factor);
         }
      }
   }

   return float4(color, transmittance);
}

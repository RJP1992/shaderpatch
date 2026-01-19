#ifndef NORMAL_BF3_CHARACTER_COMMON_INCLUDED
#define NORMAL_BF3_CHARACTER_COMMON_INCLUDED

#include "constants_list.hlsl"
#include "lighting_utilities.hlsl"
#include "pixel_utilities.hlsl"

cbuffer MaterialConstants : register(MATERIAL_CB_INDEX)
{
   float3 base_diffuse_color;
   float  gloss_map_weight;
   float3 base_specular_color;
   float  specular_exponent;
   bool   use_ao_texture;
   bool   use_emissive_texture;
   float  emissive_texture_scale;
   float  emissive_power;
   bool   use_env_map;
   float  env_map_vis;
   float  dynamic_normal_sign;
   bool   use_outline_light;
   float3 outline_light_color;
   float  outline_light_width; 
   float  outline_light_fade;
   float  height_scale;
   float  parallax_scale_x;
   // Character shader specific
   bool   use_reflection;
   float  reflection_fresnel_power;
   float  reflection_intensity;
   float  reflection_lum_power;
};

// clang-format off

const static bool normal_bf3_character_use_shadow_map = SP_USE_STENCIL_SHADOW_MAP;
const static bool normal_bf3_character_use_projected_texture = SP_USE_PROJECTED_TEXTURE;

struct surface_info {
   float3 normalWS;
   float3 positionWS;
   float3 viewWS;
   float3 diffuse_color; 
   float3 static_diffuse_lighting; 
   float3 specular_color;
   float3 vertex_color;
   float shadow;
   float ao;
};

//------------------------------------------------------------------------------
// BF3 character shader style Blinn-Phong
// - Epsilon on NdotH
// - Diffuse masking on specular
//------------------------------------------------------------------------------
namespace bf3_character_light {

void calculate(inout float4 in_out_diffuse, inout float4 in_out_specular,
               float3 N, float3 V, float3 L, float attenuation,
               float3 light_color, float shadow_factor, float exponent)
{
   const float3 H = normalize(L + V);
   const float NdotH = saturate(dot(N, H));
   const float NdotL = saturate(dot(N, L));
   
   const float diffuse = NdotL * attenuation;
   float specular = pow(NdotH + 0.0001f, exponent) * attenuation;
   specular *= (diffuse != 0) ? 1.0 : 0.0;

   in_out_diffuse += (diffuse * float4(light_color, shadow_factor));
   in_out_specular += (specular * float4(light_color, shadow_factor));
}

// Game-style point light attenuation: 1/(1 + pow(dist, power))
// Different from SP's inverse-square falloff
float attenuation_point_game(float3 unnormalized_light_vector, float atten_power)
{
   float dist = length(unnormalized_light_vector);
   return 1.0 / (1.0 + pow(dist, atten_power));
}

void calculate_point(inout float4 in_out_diffuse, inout float4 in_out_specular,
                     float3 normal, float3 position, float3 view_normal,
                     float3 light_position, float inv_range_sq, float3 light_color,
                     float shadow_factor, float exponent)
{
   const float3 unnormalized_light_vector = light_position - position;
   const float3 light_dir = normalize(unnormalized_light_vector);
   
   // Game-style attenuation: 1/(1 + pow(dist, power))
   // Using power of 2.0 as default (similar to inverse square but smoother)
   const float atten_power = 2.0;
   const float attenuation = attenuation_point_game(unnormalized_light_vector, atten_power);

   calculate(in_out_diffuse, in_out_specular, normal, view_normal, light_dir,
             attenuation, light_color, shadow_factor, exponent);
}

void calculate_spot(inout float4 in_out_diffuse, inout float4 in_out_specular,
                    float3 normalWS, float3 positionWS, float3 view_normalWS,
                    float3 light_positionWS, float inv_range_sq, float3 cone_directionWS, 
                    float cone_outer_param, float cone_inner_param,
                    float3 light_color, float shadow_factor, float exponent)
{
   const float3 unnormalized_light_vectorWS = light_positionWS - positionWS;
   const float3 light_dirWS = normalize(unnormalized_light_vectorWS);

   const float theta = max(dot(-light_dirWS, cone_directionWS), 0.0);
   const float cone_falloff = saturate((theta - cone_outer_param) * cone_inner_param);

   // Game-style attenuation
   const float atten_power = 2.0;
   const float attenuation = attenuation_point_game(unnormalized_light_vectorWS, atten_power);

   calculate(in_out_diffuse, in_out_specular, normalWS, view_normalWS,
             light_dirWS, attenuation * cone_falloff, light_color, 
             shadow_factor, exponent);
}

}

//------------------------------------------------------------------------------
// Main lighting function - character shader style
//------------------------------------------------------------------------------

float3 do_lighting(surface_info surface, Texture2D<float3> projected_light_texture)
{
   const float3 positionWS = surface.positionWS;
   const float3 normalWS = surface.normalWS;
   const float3 viewWS = surface.viewWS;

   float4 diffuse_lighting = {(light::ambient(normalWS) + surface.static_diffuse_lighting) * surface.ao, 0.0};
   float4 specular_lighting = 0.0;

   [branch]
   if (light_active) {
      const float3 projected_light_texture_color = 
         normal_bf3_character_use_projected_texture ? sample_projected_light(projected_light_texture, 
                                                               mul(float4(positionWS, 1.0), light_proj_matrix)) 
                                          : 0.0;

      Lights_context context = acquire_lights_context();

      while (!context.directional_lights_end()) {
         Directional_light directional_light = context.next_directional_light();

         float3 light_color = directional_light.color;

         if (directional_light.use_projected_texture()) {
            light_color *= projected_light_texture_color;
         }

         bf3_character_light::calculate(diffuse_lighting, specular_lighting, normalWS, viewWS,
                                      -directional_light.directionWS, 1.0, light_color, 
                                      directional_light.stencil_shadow_factor(), specular_exponent);
      }

      while (!context.point_lights_end()) {
         Point_light point_light = context.next_point_light();

         float3 light_color = point_light.color;

         if (point_light.use_projected_texture()) {
            light_color *= projected_light_texture_color;
         }

         bf3_character_light::calculate_point(diffuse_lighting, specular_lighting, normalWS,
                                            positionWS, viewWS, point_light.positionWS, 
                                            point_light.inv_range_sq, light_color, 
                                            point_light.stencil_shadow_factor(), specular_exponent);
      }
      
      while (!context.spot_lights_end()) {
         Spot_light spot_light = context.next_spot_light();

         float3 light_color = spot_light.color;

         if (spot_light.use_projected_texture()) {
            light_color *= projected_light_texture_color;
         }

         bf3_character_light::calculate_spot(diffuse_lighting, specular_lighting, normalWS,
                                           positionWS, viewWS, spot_light.positionWS, 
                                           spot_light.inv_range_sq, spot_light.directionWS,
                                           spot_light.cone_outer_param, spot_light.cone_inner_param, 
                                           light_color, spot_light.stencil_shadow_factor(), specular_exponent);
      }

      if (normal_bf3_character_use_shadow_map) {
         const float shadow_diffuse_mask = saturate(diffuse_lighting.a);
         diffuse_lighting.rgb *= saturate((1.0 - (shadow_diffuse_mask * (1.0 - surface.shadow))));

         const float shadow_specular_mask = saturate(specular_lighting.a);
         specular_lighting.rgb *= saturate((1.0 - (shadow_specular_mask * (1.0 - surface.shadow))));
      }

      // Character shader style: specfinal * trans.colour.xyz * vtx_col_mult * s.xyz * 2.0f
      // Also tints specular by diffuse color
      const float specular_multiplier = 2.0;

      float3 color = 
         (diffuse_lighting.rgb * surface.diffuse_color) + 
         (specular_lighting.rgb * surface.specular_color * surface.vertex_color * surface.diffuse_color * specular_multiplier);

      color *= lighting_scale;

      if (use_outline_light) {
         const float inv_NdotV = 1 - saturate(dot(surface.normalWS, surface.viewWS));
         const float LdotN = saturate(dot(-light_directional_dir(0).xyz, surface.normalWS));
   
         float3 outline_color = smoothstep(1.0 - outline_light_width, 1.0, inv_NdotV) * lerp(LdotN, 1.0, outline_light_fade);
         outline_color *= outline_light_color;

         color *= (0.5 + outline_color) * 2.0;
      }

      return color;
   }
   else {
      return surface.diffuse_color * lighting_scale;
   }
   
   return 0.0;
}


#endif
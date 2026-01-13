
#include "adaptive_oit.hlsl"
#include "constants_list.hlsl"
#include "generic_vertex_input.hlsl"
#include "pixel_sampler_states.hlsl"
#include "pixel_utilities.hlsl"
#include "vertex_transformer.hlsl"
#include "vertex_utilities.hlsl"

// clang-format off

// Textures
Texture2D<float4> color_map : register(ps, t7);      // Main hologram texture
Texture2D<float4> noise_map : register(ps, t8);      // Noise texture for interference

// Game Constants
const static float4 blend_constant = ps_custom_constants[0];
const static float4 x_texcoords_transform = custom_constants[1];
const static float4 y_texcoords_transform = custom_constants[2];

cbuffer MaterialConstants : register(MATERIAL_CB_INDEX)
{
   // Main color bands (like SWBF3 colourbands[4])
   float3 color_primary;
   float  alpha_multiplier;
   float3 color_secondary;
   float  scanline_intensity;
   float3 color_edge;           // Fresnel edge color
   float  scanline_density;
   float3 color_glow;           // Glow/bloom color
   float  scanline_speed;
   
   // Effect controls
   float  fresnel_power;        // Edge glow falloff
   float  fresnel_intensity;    // Edge glow strength
   float  noise_intensity;      // Interference noise amount
   float  noise_scale;          // Noise texture tiling
   float  noise_speed;          // Noise animation speed
   float  flicker_intensity;    // Random brightness flicker
   float  flicker_speed;        // Flicker rate
   float  glow_intensity;       // Overall glow/emissive strength
   
   // Distortion
   float  distort_intensity;    // UV distortion amount
   float  distort_speed;        // Distortion animation speed
};

struct Vs_output
{
   float2 texcoords : TEXCOORDS;
   float3 normalWS : NORMAL;
   float3 positionWS : POSITIONWS;
   
   float fade : FADE;
   float fog : FOG;
   float4 color : COLOR;

   float4 positionPS : SV_Position;
};

Vs_output main_vs(Vertex_input input)
{
   Vs_output output;
   
   Transformer transformer = create_transformer(input);

   const float3 positionWS = transformer.positionWS();
   const float4 positionPS = transformer.positionPS();
   const float3 normalWS = transformer.normalWS();

   output.positionPS = positionPS;
   output.positionWS = positionWS;
   output.normalWS = normalWS;
   output.texcoords = transformer.texcoords(x_texcoords_transform, y_texcoords_transform);
   output.color = get_material_color(input.color());
   output.fade = calculate_near_fade_transparent(positionPS);
   output.fog = calculate_fog(positionWS, positionPS);

   output.color.rgb *= output.color.a;

   return output;
}

// Simple hash function for pseudo-random flicker
float hash(float n)
{
   return frac(sin(n) * 43758.5453123);
}

// Animated noise value
float get_flicker(float time, float speed)
{
   float t = time * speed;
   float f1 = hash(floor(t));
   float f2 = hash(floor(t) + 1.0);
   return lerp(f1, f2, frac(t));
}

struct Ps_input
{
   float2 texcoords : TEXCOORDS;
   float3 normalWS : NORMAL;
   float3 positionWS : POSITIONWS;
   
   float fade : FADE;
   float fog : FOG;
   float4 color : COLOR;
   
   float4 positionSS : SV_Position;
};

float4 main_ps(Ps_input input) : SV_Target0
{
   float time = time_seconds;
   
   // Calculate view direction for fresnel
   float3 viewWS = normalize(view_positionWS - input.positionWS);
   float3 normalWS = normalize(input.normalWS);
   float NdotV = saturate(dot(normalWS, viewWS));
   
   // Fresnel edge glow (stronger at glancing angles)
   float fresnel = pow(1.0 - NdotV, fresnel_power) * fresnel_intensity;
   
   // Animated UV distortion
   float2 distort_uv = input.texcoords;
   if (distort_intensity > 0.0) {
      float distort_wave = sin(input.texcoords.y * 20.0 + time * distort_speed);
      distort_uv.x += distort_wave * distort_intensity * 0.01;
   }
   
   // Sample main texture
   float4 base_color = color_map.Sample(aniso_wrap_sampler, distort_uv);
   
   // Scanlines
   float scanline = 1.0;
   if (scanline_intensity > 0.0) {
      float scan_pos = input.positionSS.y * scanline_density + time * scanline_speed;
      scanline = 1.0 - (sin(scan_pos) * 0.5 + 0.5) * scanline_intensity;
   }
   
   // Noise/interference
   float noise = 1.0;
   if (noise_intensity > 0.0) {
      float2 noise_uv = input.texcoords * noise_scale + float2(time * noise_speed, 0.0);
      float4 noise_sample = noise_map.Sample(linear_wrap_sampler, noise_uv);
      noise = lerp(1.0, noise_sample.r, noise_intensity);
   }
   
   // Flicker
   float flicker = 1.0;
   if (flicker_intensity > 0.0) {
      flicker = lerp(1.0, get_flicker(time, flicker_speed), flicker_intensity);
   }
   
   // Combine base color with primary tint
   float3 holo_color = base_color.rgb * color_primary;
   
   // Add secondary color based on texture green channel (if present)
   holo_color = lerp(holo_color, base_color.rgb * color_secondary, base_color.g * 0.5);
   
   // Apply effects
   holo_color *= scanline;
   holo_color *= noise;
   holo_color *= flicker;
   
   // Add fresnel edge glow
   holo_color += color_edge * fresnel;
   
   // Add overall glow
   holo_color += color_glow * glow_intensity * base_color.a;
   
   // Apply vertex color
   holo_color *= input.color.rgb;
   
   // Apply lighting scale for HDR/bloom
   holo_color *= lighting_scale;
   
   // Apply fog
   holo_color = apply_fog(holo_color, input.fog);
   
   // Calculate alpha
   float alpha = base_color.a * input.color.a * alpha_multiplier;
   alpha += fresnel * 0.5;  // Add some alpha at edges for glow
   alpha *= flicker;
   alpha = lerp(1.0, alpha, blend_constant.b);
   alpha = saturate(alpha * input.fade);
   
   // Premultiplied alpha correction
   holo_color /= max(alpha, 1e-5);
   
   return float4(holo_color, alpha);
}

[earlydepthstencil]
void oit_main_ps(Ps_input input, uint coverage : SV_Coverage)
{
   float4 color = main_ps(input);
   aoit::write_pixel((uint2)input.positionSS.xy, input.positionSS.z, color, coverage);
}

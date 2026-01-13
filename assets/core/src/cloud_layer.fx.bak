// Cloud Layer Shader for Shader Patch
// Inspired by BF3 volumetric cloud techniques, adapted for layered approach
// Use multiple instances at different scales for parallax effect

#include "constants_list.hlsl"
#include "generic_vertex_input.hlsl"
#include "pixel_sampler_states.hlsl"
#include "vertex_transformer.hlsl"
#include "vertex_utilities.hlsl"

// clang-format off

// Cloud noise texture - tileable noise (Perlin, Worley, or mixed)
Texture2D<float4> cloud_noise_tex : register(ps, t7);

// Optional: secondary detail noise for variation
Texture2D<float4> detail_noise_tex : register(ps, t8);

// Game Constants (from world/sky config)
const static float4 x_texcoords_transform = custom_constants[1];
const static float4 y_texcoords_transform = custom_constants[2];

cbuffer MaterialConstants : register(MATERIAL_CB_INDEX)
{
   float layer_scale;
   float layer_height;
   float scroll_angle;      // Wind direction in degrees (0=+X, 90=+Z, 180=-X, 270=-Z)
   float scroll_speed;
   
   float cloud_threshold;
   float cloud_softness;
   float cloud_density;
   float detail_scale;
   
   float detail_strength;
   float edge_fade_start;
   float edge_fade_end;
   float height_fade_start;
   
   float3 cloud_color_lit;
   float lighting_wrap;
   
   float3 cloud_color_dark;
   float sun_color_influence;
   
   float horizon_fade_start;
   float horizon_fade_end;
   float cloud_brightness;
   float min_brightness;
   
   float near_fade_start;
   float near_fade_end;
   float world_scale;
   float fresnel_power;      // Higher = sharper fade at grazing angles (2-5 typical)
   
   float fresnel_strength;   // 0 = no fresnel fade, 1 = full fade at edges
   float depth_bias;         // Push clouds back in depth to avoid terrain sorting (0.0001-0.001)
   float _pad0;
   float _pad1;
};

struct Vs_output
{
   float2 texcoords : TEXCOORDS0;
   float3 worldPos : TEXCOORDS1;
   float3 viewDir : TEXCOORDS2;
   float3 worldNormal : TEXCOORDS3;  // For fresnel-based edge fade
   float4 color : COLOR;
   float4 positionPS : SV_Position;
};

Vs_output main_vs(Vertex_input input)
{
   Vs_output output;
   
   Transformer transformer = create_transformer(input);

   float3 positionWS = transformer.positionWS();
   float4 positionPS = transformer.positionPS();

   output.positionPS = positionPS;
   
   // Apply depth bias to push clouds back, avoiding terrain sorting issues
   output.positionPS.z += depth_bias * output.positionPS.w;
   
   output.texcoords = transformer.texcoords(x_texcoords_transform, y_texcoords_transform);
   output.worldPos = positionWS;
   output.viewDir = normalize(positionWS - view_positionWS);
   output.worldNormal = transformer.normalWS();  // Surface normal for fresnel
   
   // Use raw vertex color to preserve alpha for edge fading
   output.color = input.color();

   return output;
}

// Attempt to extract sun direction from light system
float3 get_sun_direction()
{
   // Primary directional light is typically the sun
   return normalize(light_directional_dir(0));
}

float3 get_sun_color()
{
   return light_directional_color(0).rgb;
}

float4 main_ps(Vs_output input) : SV_Target0
{
   float3 viewDir = normalize(input.viewDir);
   float3 worldPos = input.worldPos;
   float3 worldNormal = normalize(input.worldNormal);
   
   // === FRESNEL-BASED EDGE FADE ===
   // Fades cloud at grazing angles - hides dome edges when looking down from above
   // NdotV is 1 when looking straight at surface, 0 at grazing angles
   float NdotV = abs(dot(worldNormal, -viewDir));
   float fresnelFade = pow(NdotV, fresnel_power);
   fresnelFade = lerp(1.0, fresnelFade, fresnel_strength);
   
   // === HORIZON FADE ===
   // Fade clouds near horizon to avoid hard cutoff
   // Use abs() so clouds are visible from both above and below
   float verticalAmount = abs(viewDir.y);
   float horizonFade = smoothstep(horizon_fade_end, horizon_fade_start, verticalAmount);
   
   // Early out if at horizon (looking horizontally through cloud layer)
   if (horizonFade <= 0.001)
      discard;
   
   // === EDGE FADE based on world position ===
   // Fade out at dome edges using distance from world center (XZ plane)
   float distFromCenter = length(worldPos.xz);
   float edgeFade = 1.0 - smoothstep(edge_fade_start, edge_fade_end, distFromCenter);
   
   // Also fade based on height (Y) - fade near bottom of dome
   float heightFade = smoothstep(0.0, height_fade_start, worldPos.y);
   
   // === NEAR CAMERA FADE ===
   // Fade clouds when camera gets very close - smoother transition when flying through
   float distToCamera = length(worldPos - view_positionWS);
   float nearFade = smoothstep(near_fade_end, near_fade_start, distToCamera);
   
   // === UV CALCULATION USING WORLD POSITION ===
   // Use world XZ for noise sampling - eliminates UV stretching
   float2 baseUV = worldPos.xz * layer_scale * world_scale;
   
   // Offset per layer using layer_height as a seed - breaks up repetition between layers
   float2 layerOffset = float2(layer_height * 0.01, layer_height * 0.007);
   baseUV += layerOffset;
   
   // Time-based scrolling using angle for direction
   float angleRad = scroll_angle * 3.14159265 / 180.0;
   float2 scrollDir = float2(cos(angleRad), sin(angleRad));
   float2 scrollOffset = scrollDir * scroll_speed * time_seconds;
   
   // === DUAL-OCTAVE NOISE SAMPLING (BF3-style) ===
   // Primary noise - large cloud shapes
   float2 uv1 = baseUV + scrollOffset;
   float noise1 = cloud_noise_tex.Sample(aniso_wrap_sampler, uv1).r;
   
   // Secondary noise - smaller detail, scrolls slightly faster
   float2 uv2 = baseUV * detail_scale + scrollOffset * 1.3;
   float noise2 = cloud_noise_tex.Sample(aniso_wrap_sampler, uv2).r;
   
   // Optional detail texture for more variation
   float2 uvDetail = baseUV * detail_scale * 2.0 + scrollOffset * 0.7;
   float detailNoise = detail_noise_tex.Sample(aniso_wrap_sampler, uvDetail).r;
   
   // === CLOUD DENSITY CALCULATION ===
   // Blend primary and secondary noise (BF3 uses weighted octaves)
   float combinedNoise = noise1 * 0.6 + noise2 * 0.3 + detailNoise * detail_strength * 0.1;
   
   // Apply threshold to create cloud shapes
   float rawDensity = combinedNoise - cloud_threshold;
   
   // Soft edge using smoothstep (approximates BF3's density falloff)
   float density = smoothstep(0.0, cloud_softness, rawDensity);
   
   // === SUN-FACING LIGHTING (simplified ray-march approximation) ===
   float3 sunDir = get_sun_direction();
   
   // Calculate how much this point faces the sun
   // Wrap parameter allows light to wrap around cloud edges
   float sunDot = dot(viewDir, -sunDir);
   float sunFacing = saturate(sunDot * (1.0 - lighting_wrap) + lighting_wrap);
   
   // Add subtle variation based on noise for self-shadowing approximation
   float selfShadow = saturate(noise1 * 0.5 + 0.5);
   sunFacing *= lerp(0.7, 1.0, selfShadow);
   
   // === FINAL COLOR ===
   // Interpolate between dark and lit colors based on sun facing
   float3 cloudColor = lerp(cloud_color_dark, cloud_color_lit, sunFacing);
   
   // Tint by sun color for time-of-day
   float3 sunColor = get_sun_color();
   cloudColor *= lerp(float3(1, 1, 1), sunColor, sun_color_influence);
   
   // Apply vertex color (allows world-based tinting)
   cloudColor *= input.color.rgb;
   
   // === FINAL ALPHA ===
   float alpha = density * cloud_density * horizonFade * edgeFade * heightFade * nearFade * fresnelFade;
   alpha *= input.color.a;
   
   // Apply lighting scale with minimum brightness floor
   float effectiveLighting = max(lighting_scale, min_brightness);
   cloudColor *= effectiveLighting * cloud_brightness;
   
   // Premultiplied alpha output for proper blending
   return float4(cloudColor * alpha, alpha);
}

// === VARIANT: Dense cumulus-style clouds ===
float4 cumulus_ps(Vs_output input) : SV_Target0
{
   float3 viewDir = normalize(input.viewDir);
   float3 worldNormal = normalize(input.worldNormal);
   
   // Fresnel fade
   float NdotV = abs(dot(worldNormal, -viewDir));
   float fresnelFade = pow(NdotV, fresnel_power);
   fresnelFade = lerp(1.0, fresnelFade, fresnel_strength);
   
   float verticalAmount = abs(viewDir.y);
   float horizonFade = smoothstep(horizon_fade_end, horizon_fade_start, verticalAmount);
   if (horizonFade <= 0.001)
      discard;
   
   float2 baseUV = input.texcoords * layer_scale;
   float angleRad = scroll_angle * 3.14159265 / 180.0;
   float2 scrollDir = float2(cos(angleRad), sin(angleRad));
   float2 scrollOffset = scrollDir * scroll_speed * time_seconds;
   
   // More aggressive noise combination for puffy cumulus
   float2 uv1 = baseUV + scrollOffset;
   float2 uv2 = baseUV * 2.0 + scrollOffset * 1.2;
   float2 uv3 = baseUV * 4.0 + scrollOffset * 1.5;
   
   float n1 = cloud_noise_tex.Sample(aniso_wrap_sampler, uv1).r;
   float n2 = cloud_noise_tex.Sample(aniso_wrap_sampler, uv2).r;
   float n3 = cloud_noise_tex.Sample(aniso_wrap_sampler, uv3).r;
   
   // Weighted blend favoring large shapes
   float combinedNoise = n1 * 0.5 + n2 * 0.3 + n3 * 0.2;
   
   // Sharper edges for cumulus
   float rawDensity = combinedNoise - cloud_threshold;
   float density = smoothstep(0.0, cloud_softness * 0.5, rawDensity);
   density = pow(density, 0.8); // Boost density
   
   // Stronger lighting contrast for cumulus
   float3 sunDir = get_sun_direction();
   float sunFacing = saturate(dot(viewDir, -sunDir) * 0.6 + 0.4);
   
   // Fake ambient occlusion at cloud edges
   float edgeAO = smoothstep(0.0, 0.3, density);
   sunFacing *= lerp(0.5, 1.0, edgeAO);
   
   float3 cloudColor = lerp(cloud_color_dark, cloud_color_lit, sunFacing);
   cloudColor *= input.color.rgb * lighting_scale;
   
   float alpha = density * cloud_density * horizonFade * fresnelFade * input.color.a;
   
   return float4(cloudColor * alpha, alpha);
}

// === VARIANT: Wispy cirrus-style clouds ===
float4 cirrus_ps(Vs_output input) : SV_Target0
{
   float3 viewDir = normalize(input.viewDir);
   float3 worldNormal = normalize(input.worldNormal);
   
   // Fresnel fade
   float NdotV = abs(dot(worldNormal, -viewDir));
   float fresnelFade = pow(NdotV, fresnel_power);
   fresnelFade = lerp(1.0, fresnelFade, fresnel_strength);
   
   float verticalAmount = abs(viewDir.y);
   float horizonFade = smoothstep(horizon_fade_end, horizon_fade_start, verticalAmount);
   if (horizonFade <= 0.001)
      discard;
   
   float2 baseUV = input.texcoords * layer_scale;
   float angleRad = scroll_angle * 3.14159265 / 180.0;
   float2 scrollDir = float2(cos(angleRad), sin(angleRad));
   float2 scrollOffset = scrollDir * scroll_speed * time_seconds;
   
   // Stretched UVs for wispy appearance
   float2 stretchedUV = baseUV * float2(1.0, 0.3);
   
   float2 uv1 = stretchedUV + scrollOffset;
   float2 uv2 = stretchedUV * 3.0 + scrollOffset * 2.0;
   
   float n1 = cloud_noise_tex.Sample(aniso_wrap_sampler, uv1).r;
   float n2 = cloud_noise_tex.Sample(aniso_wrap_sampler, uv2).r;
   
   // Thin, wispy blend
   float combinedNoise = n1 * 0.7 + n2 * 0.3;
   
   // Very soft edges for cirrus
   float rawDensity = combinedNoise - cloud_threshold;
   float density = smoothstep(0.0, cloud_softness * 2.0, rawDensity);
   density *= 0.6; // Cirrus is thin
   
   // Subtle lighting for high-altitude feel
   float3 sunDir = get_sun_direction();
   float sunFacing = saturate(dot(viewDir, -sunDir) * 0.3 + 0.7);
   
   float3 cloudColor = lerp(cloud_color_dark, cloud_color_lit, sunFacing);
   cloudColor *= input.color.rgb * lighting_scale;
   
   float alpha = density * cloud_density * horizonFade * fresnelFade * input.color.a;
   
   return float4(cloudColor * alpha, alpha);
}

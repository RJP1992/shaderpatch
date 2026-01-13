// Distance Fog Post-Process Effect
// With height fog, blend modes, and noise

Texture2D<float3> scene_input : register(t0);
Texture2D<float> depth_near : register(t1);
Texture2D<float> depth_far : register(t2);
Texture2D<float3> noise_texture : register(t3);

SamplerState linear_wrap_sampler : register(s0);

cbuffer FogConstants : register(b1)
{
   float4x4 inv_view_proj;
   
   float3 fog_color;
   float fog_density;
   
   float3 camera_position;
   float fog_start;
   
   float fog_end;
   float height_falloff;
   float height_min;
   float height_max;
   
   int blend_mode;
   int height_mode;
   float noise_scale;
   float noise_intensity;
   
   float time;
   float3 _padding;
}

float4 fog_main_ps(float2 texcoords : TEXCOORD, float4 positionSS : SV_Position) : SV_Target
{
   float3 scene = scene_input[positionSS.xy];
   float dn = depth_near[positionSS.xy];
   float df = depth_far[positionSS.xy];
   
	// Don't skip anything based on depth - reconstruct distance first
   float depth = (dn < df) ? dn : df;  // Use closer depth
   
   float4 clip;
   clip.x = texcoords.x * 2.0 - 1.0;
   clip.y = (1.0 - texcoords.y) * 2.0 - 1.0;
   clip.z = depth;
   clip.w = 1.0;
   
   float4 world = mul(inv_view_proj, clip);
   float3 world_pos = world.xyz / world.w;
   float dist = length(world_pos - camera_position);
   
   // If distance is absurdly large, it's sky - skip
   if (dist > 50000.0)
      return float4(scene, 1.0);
	  
   // Linear fog between start and end
   float fog_range = fog_end - fog_start;
   float fog_factor = saturate((dist - fog_start) / max(fog_range, 0.001));
   fog_factor *= fog_density;
   
   // Height fog - thicker at low elevations
   if (height_falloff > 0.0)
   {
      float height_factor = 1.0;
      
      if (height_mode == 0)
      {
         // Above only - fog fades above height_max
         if (world_pos.y > height_max)
            height_factor = exp(-(world_pos.y - height_max) * height_falloff);
      }
      else if (height_mode == 1)
      {
         // Below only - fog fades below height_min
         if (world_pos.y < height_min)
            height_factor = exp(-(height_min - world_pos.y) * height_falloff);
      }
      else
      {
         // Both - fog densest in band, fades both directions
         if (world_pos.y < height_min)
            height_factor = exp(-(height_min - world_pos.y) * height_falloff);
         else if (world_pos.y > height_max)
            height_factor = exp(-(world_pos.y - height_max) * height_falloff);
      }
      
      fog_factor *= height_factor;
   }
   
   // Noise (BF3 style - dual plane sampling)
   if (noise_scale > 0.0)
   {
      float3 noise_pos = world_pos + float3(time, 0, time * 0.7);
      float2 uv_xy = noise_pos.xy / noise_scale;
      float2 uv_yz = noise_pos.yz / noise_scale;
      
      float noise_xy = noise_texture.SampleLevel(linear_wrap_sampler, uv_xy, 0).r;
      float noise_yz = noise_texture.SampleLevel(linear_wrap_sampler, uv_yz, 0).r;
      float noise_val = (noise_xy + noise_yz) * 0.5;
      
      fog_factor += (noise_val - 0.5) * noise_intensity * fog_factor;
   }
   
   fog_factor = saturate(fog_factor);
   
   // Apply blend mode
   float3 result;
   if (blend_mode == 1)
   {
      // Tinted - mix fog with scene color
      float3 tinted_fog = lerp(fog_color, scene, 0.3);
      result = lerp(scene, tinted_fog, fog_factor);
   }
   else if (blend_mode == 2)
   {
      // Atmospheric - desaturate and blend
      float luminance = dot(scene, float3(0.299, 0.587, 0.114));
      float3 desaturated = lerp(scene, float3(luminance, luminance, luminance), fog_factor * 0.5);
      result = lerp(desaturated, fog_color, fog_factor);
   }
   else if (blend_mode == 3)
   {
      // Additive
      result = scene + fog_color * fog_factor;
   }
   else if (blend_mode == 4)
   {
      // Screen blend
      result = 1.0 - (1.0 - scene) * (1.0 - fog_color * fog_factor);
   }
   else
   {
      // Normal (0)
      result = lerp(scene, fog_color, fog_factor);
   }
   
   return float4(result, 1.0);
}

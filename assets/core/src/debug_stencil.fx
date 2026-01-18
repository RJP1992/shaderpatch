// Debug Stencil Visualizer
// Simple standalone effect to visualize depth-stencil buffers
// Uses shared postprocess vertex shader

Texture2D<float> depth_texture : register(t0);
Texture2D<uint2> stencil_texture : register(t1);

cbuffer DebugStencilConstants : register(b0)
{
   int mode;           // 0=depth, 1=stencil color-coded, 2=stencil raw, 3=both
   int use_near;       // 1=near buffer, 0=far buffer
   float2 _pad;
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
   int2 pixel = int2(input.positionPS.xy);

   // Sample depth
   float depth = depth_texture.Load(int3(pixel, 0));

   // Sample stencil - data is in .y (G component) for X24_TYPELESS_G8_UINT
   uint2 stencil_raw = stencil_texture.Load(int3(pixel, 0));
   uint stencil = stencil_raw.y;  // Stencil is in .y, not .x

   if (mode == 0)
   {
      // Mode 0: Depth visualization (amplified near 1.0)
      float vis = saturate((depth - 0.99) * 100.0);
      return float4(vis, vis, vis, 1.0);
   }
   else if (mode == 1)
   {
      // Mode 1: Stencil color-coded
      float3 c = float3(0, 0, 0);
      if      (stencil == 0) c = float3(0.0, 0.0, 0.0);  // Black
      else if (stencil == 1) c = float3(1.0, 0.0, 0.0);  // Red
      else if (stencil == 2) c = float3(0.0, 1.0, 0.0);  // Green
      else if (stencil == 3) c = float3(0.0, 0.0, 1.0);  // Blue
      else if (stencil == 4) c = float3(1.0, 1.0, 0.0);  // Yellow
      else if (stencil == 5) c = float3(1.0, 0.0, 1.0);  // Magenta
      else if (stencil == 6) c = float3(0.0, 1.0, 1.0);  // Cyan
      else if (stencil == 7) c = float3(1.0, 0.5, 0.0);  // Orange
      else                   c = float3(1.0, 1.0, 1.0);  // White (8+)
      return float4(c, 1.0);
   }
   else if (mode == 2)
   {
      // Mode 2: Raw stencil value as grayscale (normalized 0-255)
      return float4(stencil / 255.0, stencil / 255.0, stencil / 255.0, 1.0);
   }
   else
   {
      // Mode 3: Combined - depth as brightness, stencil as hue
      float brightness = saturate((depth - 0.9) * 10.0);
      float3 hue = float3(0.2, 0.2, 0.2);  // Default gray (stencil 0)
      if      (stencil == 1) hue = float3(1.0, 0.0, 0.0);
      else if (stencil == 2) hue = float3(0.0, 1.0, 0.0);
      else if (stencil == 3) hue = float3(0.0, 0.0, 1.0);
      else if (stencil >= 4) hue = float3(1.0, 1.0, 0.0);
      return float4(hue * brightness, 1.0);
   }
}

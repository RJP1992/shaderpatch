// Cubemap Debug Visualizer
// Renders a debug cube showing cubemap alignment for SWBF2 skybox matching
// Can render at infinity (like a skybox) or at a fixed world distance

TextureCube<float3> cubemap : register(t0);
Texture2D<float> depth_near : register(t1);
Texture2D<float> depth_far : register(t2);
SamplerState linear_clamp_sampler : register(s0);

cbuffer DebugConstants : register(b0)
{
   float4x4 inv_view_proj;           // 64 bytes

   float3 camera_position;           // 12 bytes
   float debug_distance;             // 4 bytes (world distance when not at infinity)

   // Cubemap alignment transform (same as fog shader)
   float3x3 cubemap_rotation;        // 48 bytes (3 rows of float4 with padding)
   float3 cubemap_scale;             // 12 bytes
   float _pad_scale;                 // 4 bytes padding
   float3 cubemap_offset;            // 12 bytes
   float _pad_offset;                // 4 bytes padding

   int render_at_infinity;           // 4 bytes (1 = infinity, 0 = fixed distance)
   int show_grid;                    // 4 bytes (1 = show wireframe grid)
   float grid_thickness;             // 4 bytes
   float _pad;                       // 4 bytes padding
};

// Input from postprocess vertex shader
struct Vs_input
{
   float2 texcoords : TEXCOORD;
   float2 positionSS : POSITION;
   float4 positionPS : SV_Position;
};

// Get face name color based on dominant axis
float3 get_face_color(float3 dir)
{
   float3 abs_dir = abs(dir);
   float max_axis = max(abs_dir.x, max(abs_dir.y, abs_dir.z));

   if (abs_dir.x == max_axis)
      return dir.x > 0 ? float3(1.0, 0.2, 0.2) : float3(0.5, 0.1, 0.1);  // +X red, -X dark red
   else if (abs_dir.y == max_axis)
      return dir.y > 0 ? float3(0.2, 1.0, 0.2) : float3(0.1, 0.5, 0.1);  // +Y green, -Y dark green
   else
      return dir.z > 0 ? float3(0.2, 0.2, 1.0) : float3(0.1, 0.1, 0.5);  // +Z blue, -Z dark blue
}

// Compute grid lines at cube edges
float compute_grid(float3 dir, float thickness)
{
   float3 abs_dir = abs(dir);
   float max_axis = max(abs_dir.x, max(abs_dir.y, abs_dir.z));

   // Normalize to cube face
   float3 cube_pos = dir / max_axis;

   // Find which face we're on
   float3 face_uv;
   if (abs_dir.x == max_axis)
      face_uv = float3(cube_pos.y, cube_pos.z, 0);
   else if (abs_dir.y == max_axis)
      face_uv = float3(cube_pos.x, cube_pos.z, 0);
   else
      face_uv = float3(cube_pos.x, cube_pos.y, 0);

   // Compute distance to edges
   float2 edge_dist = 1.0 - abs(face_uv.xy);
   float min_dist = min(edge_dist.x, edge_dist.y);

   // Create grid lines
   float grid = smoothstep(0.0, thickness, min_dist);

   return 1.0 - grid;  // 1 at edges, 0 elsewhere
}

float4 main_ps(Vs_input input) : SV_Target0
{
   // Sample depth buffers
   float dn = depth_near[input.positionPS.xy];
   float df = depth_far[input.positionPS.xy];
   float depth = min(dn, df);

   // Reconstruct world position from depth
   float4 clip;
   clip.x = input.texcoords.x * 2.0 - 1.0;
   clip.y = (1.0 - input.texcoords.y) * 2.0 - 1.0;
   clip.z = depth;
   clip.w = 1.0;

   float4 world_pos = mul(inv_view_proj, clip);
   world_pos /= world_pos.w;

   float scene_dist = length(world_pos.xyz - camera_position);
   float3 view_dir = normalize(world_pos.xyz - camera_position);

   // Determine if we should render at this pixel
   bool should_render = false;

   if (render_at_infinity != 0)
   {
      // At infinity - only render where depth is at far plane (sky)
      // Check raw depth value - 1.0 or very close to it means sky/far plane
      should_render = (depth > 0.9999);
   }
   else
   {
      // Fixed distance - render where cube would be (simplified: always render if debug distance < scene)
      should_render = (debug_distance < scene_dist);
   }

   if (!should_render)
      discard;

   // Apply cubemap alignment transform (same as fog shader)
   float3 lookup = view_dir;
   lookup = mul(cubemap_rotation, lookup);  // Rotate
   lookup *= cubemap_scale;                  // Scale
   lookup += cubemap_offset;                 // Offset
   lookup = normalize(lookup);               // Re-normalize

   // Sample cubemap
   float3 color = cubemap.SampleLevel(linear_clamp_sampler, lookup, 0).rgb;

   // Add debug overlays
   if (show_grid != 0)
   {
      // Compute grid on the transformed lookup direction
      float grid = compute_grid(lookup, grid_thickness);

      // Grid lines in white
      color = lerp(color, float3(1.0, 1.0, 1.0), grid * 0.5);

      // Face tint for identification
      float3 face_color = get_face_color(lookup);
      color = lerp(color, face_color, grid * 0.3);
   }

   return float4(color, 1.0);
}

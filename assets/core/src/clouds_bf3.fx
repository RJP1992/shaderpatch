// ===========================================================================
// CLOUDS_BF3.FX - BF3-Authentic Cloud Rendering
// ===========================================================================
// Tier 2 Implementation:
// - Clustered billboard particles with noise-eroded edges
// - BF3's 4-octave noise sampling from authentic textures
// - Analytical lighting with silver lining and powder effects
// - Depth-aware soft particles
// ===========================================================================

// -----------------------------------------------------------------------------
// STRUCTURES (must be defined before use in StructuredBuffer)
// -----------------------------------------------------------------------------

struct CloudParticle
{
    float3 position;
    float size;
    float3 color;
    float alpha;
    float rotation;
    float noise_offset;     // Per-particle noise phase
    float density;          // Core density multiplier
    float _pad;
};

// -----------------------------------------------------------------------------
// TEXTURES
// -----------------------------------------------------------------------------

// Scene depth for soft particle blending
Texture2D<float> depth_texture : register(t0);

// Soft circular falloff texture (generated or simple gradient)
Texture2D<float4> particle_texture : register(t1);

// Particle instance data
StructuredBuffer<CloudParticle> particle_buffer : register(t2);

// BF3 noise textures - 4 octaves packed into RGBA
Texture2D<float4> noise_octaves : register(t3);

// -----------------------------------------------------------------------------
// SAMPLERS
// -----------------------------------------------------------------------------

SamplerState sampler_linear_wrap : register(s0);
SamplerState sampler_point_clamp : register(s1);
SamplerState sampler_linear_clamp : register(s2);

// -----------------------------------------------------------------------------
// CONSTANTS
// -----------------------------------------------------------------------------

cbuffer CloudConstants : register(b1)
{
    // Matrices
    float4x4 view_matrix;
    float4x4 proj_matrix;
    float4x4 view_proj_matrix;
    
    // Camera
    float3 camera_position;
    float cloud_time;
    
    float3 camera_right;
    float global_alpha;
    
    float3 camera_up;
    float fade_near;
    
    float3 camera_forward;
    float fade_far;
    
    // Screen info
    float2 screen_size;
    float2 depth_params;        // x = projection[2][2], y = projection[3][2]
    
    // Sun lighting
    float3 sun_direction;       // Normalized, pointing TOWARDS sun
    float sun_intensity;
    
    float3 sun_color;
    float ambient_intensity;
    
    // Cloud colors
    float3 cloud_color_bright;  // Lit side
    float _pad0;
    
    float3 cloud_color_dark;    // Shadow side
    float _pad1;
    
    float3 cloud_color_ambient; // Ambient fill
    float _pad2;
    
    // Noise parameters (BF3-style)
    float4 octave_weights;      // Weight for R, G, B, A channels
    
    float noise_scale;          // UV scale for noise sampling
    float noise_erosion;        // How much noise erodes edges (0-1)
    float noise_edge_sharpness; // Sharpness of eroded edges
    float noise_animation_speed;
    
    // Lighting parameters
    float scatter_forward;      // Forward scattering (silver lining) 0-1
    float scatter_forward_exp;  // Exponent for forward scatter falloff
    float scatter_back;         // Back scattering (rim on dark side) 0-1
    float absorption;           // Light absorption through cloud
    
    float powder_strength;      // Powder/beers effect at dense regions
    float ambient_boost;        // Extra ambient in shadowed areas
    float self_shadow;          // Fake self-shadowing strength
    float depth_softness;       // Soft particle blend distance
};

// -----------------------------------------------------------------------------
// VERTEX SHADER OUTPUT
// -----------------------------------------------------------------------------

struct VS_Output
{
    float4 position_cs      : SV_Position;
    float2 uv               : TEXCOORD0;
    float3 world_position   : TEXCOORD1;
    float3 particle_center  : TEXCOORD2;
    float4 screen_pos       : TEXCOORD3;
    
    // Lighting data (computed per-vertex for efficiency)
    float3 light_color      : TEXCOORD4;
    float3 ambient_color    : TEXCOORD5;
    
    // Particle parameters
    float alpha             : TEXCOORD6;
    float noise_phase       : TEXCOORD7;
    float core_density      : TEXCOORD8;
};

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------

VS_Output cloud_vs(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    VS_Output output;
    
    // Load particle data
    CloudParticle particle = particle_buffer[instance_id];
    
    // Generate quad vertices (2 triangles, 6 vertices per particle)
    // Vertex order: 0-1-2, 2-1-3 forming a quad
    uint quad_idx = vertex_id % 6;
    float2 corner;
    
    // Map vertex ID to quad corner
    // 0--1
    // |\ |
    // | \|
    // 2--3
    if (quad_idx == 0) corner = float2(-1, -1);
    else if (quad_idx == 1) corner = float2( 1, -1);
    else if (quad_idx == 2) corner = float2(-1,  1);
    else if (quad_idx == 3) corner = float2(-1,  1);
    else if (quad_idx == 4) corner = float2( 1, -1);
    else corner = float2( 1,  1);
    
    // Apply rotation
    float s, c;
    sincos(particle.rotation, s, c);
    float2 rotated_corner;
    rotated_corner.x = corner.x * c - corner.y * s;
    rotated_corner.y = corner.x * s + corner.y * c;
    
    // Billboard in world space
    float3 world_offset = (camera_right * rotated_corner.x + camera_up * rotated_corner.y) * particle.size;
    float3 world_pos = particle.position + world_offset;
    
    output.world_position = world_pos;
    output.particle_center = particle.position;
    
    // Transform to clip space
    float4 clip_pos = mul(view_proj_matrix, float4(world_pos, 1.0));
    output.position_cs = clip_pos;
    output.screen_pos = clip_pos;
    
    // UV for particle texture (0-1 range)
    output.uv = corner * 0.5 + 0.5;
    
    // Distance-based fade
    float dist = length(particle.position - camera_position);
    float distance_fade = saturate((dist - fade_near) / (fade_far - fade_near));
    distance_fade = 1.0 - distance_fade; // Invert: 1 at near, 0 at far
    
    // Near fade (don't render particles too close to camera)
    float near_fade = saturate((dist - fade_near * 0.1) / (fade_near * 0.5));
    
    output.alpha = particle.alpha * global_alpha * distance_fade * near_fade;
    output.noise_phase = particle.noise_offset;
    output.core_density = particle.density;
    
    // ---------------------------------------------------------------------
    // PER-VERTEX LIGHTING (BF3-style)
    // ---------------------------------------------------------------------
    
    // View direction (from particle to camera)
    float3 view_dir = normalize(camera_position - particle.position);
    
    // Sun dot product: +1 = looking at sun through cloud, -1 = sun behind camera
    float sun_dot = dot(view_dir, sun_direction);
    
    // Forward scattering (silver lining effect)
    // When looking towards sun through cloud edges
    float forward_scatter = pow(saturate(sun_dot), scatter_forward_exp) * scatter_forward;
    
    // Back scattering (subtle rim on dark side)
    float back_scatter = pow(saturate(-sun_dot * 0.5 + 0.5), 2.0) * scatter_back;
    
    // Basic sun-facing illumination
    // Use particle's relative position to fake thickness
    float sun_facing = dot(normalize(particle.position - camera_position), -sun_direction);
    float illumination = saturate(sun_facing * 0.5 + 0.5);
    
    // Apply absorption (light dims as it passes through cloud)
    float thickness_approx = particle.density * absorption;
    float transmittance = exp(-thickness_approx);
    
    // Powder effect: denser areas scatter more light internally
    float powder = 1.0 - exp(-thickness_approx * 2.0);
    powder *= powder_strength;
    
    // Combine lighting
    float3 direct_light = sun_color * sun_intensity * transmittance * illumination;
    float3 scatter_light = sun_color * (forward_scatter + back_scatter + powder);
    float3 ambient_light = cloud_color_ambient * ambient_intensity;
    
    // Boost ambient in shadowed areas
    ambient_light *= (1.0 + ambient_boost * (1.0 - illumination));
    
    output.light_color = direct_light + scatter_light;
    output.ambient_color = ambient_light;
    
    return output;
}

// -----------------------------------------------------------------------------
// PIXEL SHADER HELPERS
// -----------------------------------------------------------------------------

// BF3's octave combination with turbulence
float sample_bf3_noise(float2 uv, float phase)
{
    // Animate UV with swirling turbulent motion
    float swirl = sin(cloud_time * 0.1 + phase * 6.28) * 0.02;
    float2 animated_uv = uv + float2(
        cloud_time * noise_animation_speed + swirl,
        cloud_time * noise_animation_speed * 0.4 + cos(cloud_time * 0.15) * 0.01
    ) + phase;
    
    // Sample noise texture (4 octaves in RGBA)
    float4 noise_sample = noise_octaves.Sample(sampler_linear_wrap, animated_uv);
    
    // BF3 style: center around 0.5, then weight and sum
    float4 centered = noise_sample - 0.5;
    float combined = dot(centered, octave_weights) + 0.5;
    
    return combined;
}

// Spectacular multi-octave density sampling - SOFT edges
float sample_cloud_density(float2 uv, float phase, float core_alpha)
{
    // Multiple noise layers at different scales for organic detail
    float noise1 = sample_bf3_noise(uv * noise_scale, phase);
    float noise2 = sample_bf3_noise(uv * noise_scale * 2.3 + 0.33, phase * 1.7);
    float noise3 = sample_bf3_noise(uv * noise_scale * 5.1 + 0.67, phase * 0.6);
    float noise4 = sample_bf3_noise(uv * noise_scale * 11.7, phase * 2.3);
    
    // Combine with decreasing influence
    float combined_noise = noise1 * 0.4 + noise2 * 0.3 + noise3 * 0.2 + noise4 * 0.1;
    
    // Erosion - modulates the density smoothly, not a hard cutoff
    float erosion = (combined_noise - 0.5) * 2.0; // -1 to 1
    
    // Apply erosion as a multiplier, not an additive threshold
    // This keeps soft edges soft
    float erosion_mult = saturate(0.5 + erosion * noise_erosion);
    
    // Billowy variation
    float billow = noise1 * noise2;
    float billow_mult = 0.7 + billow * 0.6;
    
    // Combine: core shape * erosion * billow
    float density = core_alpha * erosion_mult * billow_mult;
    
    // Soft power curve for contrast without hard edges
    density = pow(saturate(density), noise_edge_sharpness);
    
    return density;
}

// -----------------------------------------------------------------------------
// PIXEL SHADER - SPECTACULAR VERSION
// -----------------------------------------------------------------------------

float4 cloud_ps(VS_Output input) : SV_Target0
{
    // Early out if fully transparent
    if (input.alpha <= 0.001)
        discard;
    
    // Sample base particle shape (billowy texture)
    float4 particle_sample = particle_texture.Sample(sampler_linear_clamp, input.uv);
    float base_alpha = particle_sample.a;
    float particle_brightness = particle_sample.r;
    
    // Early out if outside particle
    if (base_alpha <= 0.001)
        discard;
    
    // ---------------------------------------------------------------------
    // SPECTACULAR NOISE EROSION
    // ---------------------------------------------------------------------
    
    // World-space noise for consistency across particles
    float2 noise_uv = input.world_position.xz * 0.001;
    
    // Add particle-local variation for unique shapes
    float2 local_uv = input.uv - 0.5;
    noise_uv += local_uv * 0.03;
    
    // Add height variation
    noise_uv.y += input.world_position.y * 0.0005;
    
    // Sample spectacular cloud density
    float density = sample_cloud_density(noise_uv, input.noise_phase, base_alpha * input.core_density);
    
    // Combine with vertex alpha
    float final_alpha = density * input.alpha * global_alpha;
    
    if (final_alpha <= 0.001)
        discard;
    
    // ---------------------------------------------------------------------
    // DEPTH TEST WITH SOFT PARTICLES
    // ---------------------------------------------------------------------
    
    float2 screen_uv = (input.screen_pos.xy / input.screen_pos.w) * float2(0.5, -0.5) + 0.5;
    float raw_depth = depth_texture.SampleLevel(sampler_point_clamp, screen_uv, 0);
    
    // Linearize depth
    float scene_z = depth_params.y / (raw_depth - depth_params.x);
    float cloud_z = input.screen_pos.w;
    
    // Depth difference: positive = cloud in front of geometry
    float depth_diff = scene_z - cloud_z;
    
    // Hide clouds behind geometry
    if (depth_diff < 0) {
        discard;
    }
    
    // Soft fade when approaching geometry
    float soft_fade = saturate(depth_diff / depth_softness);
    final_alpha *= soft_fade;
    
    if (final_alpha <= 0.001)
        discard;
    
    // ---------------------------------------------------------------------
    // SPECTACULAR LIGHTING
    // ---------------------------------------------------------------------
    
    // Self-shadowing: denser = darker
    float self_shadow_factor = 1.0 - (density * self_shadow);
    
    // Edge detection for silver lining
    float edge = 1.0 - base_alpha;
    float edge2 = edge * edge;
    
    // Dramatic silver lining effect (bright edges when backlit)
    float3 view_dir = normalize(camera_position - input.particle_center);
    float sun_facing = dot(view_dir, sun_direction);
    float silver_lining = pow(saturate(sun_facing), 2.0) * edge2 * 2.0;
    
    // Height-based coloring (golden tops at sunset, darker bases)
    float height_offset = (input.world_position.y - input.particle_center.y);
    float height_factor = saturate(height_offset / 80.0 + 0.5);
    
    // Base color with dramatic variation
    float3 base_color = lerp(cloud_color_dark, cloud_color_bright, self_shadow_factor);
    base_color *= particle_brightness;
    
    // Warmer color on sun-facing tops
    float3 warm_tint = float3(1.05, 1.0, 0.95);
    base_color = lerp(base_color, base_color * warm_tint, height_factor * 0.4);
    
    // Apply vertex lighting
    float3 lit_color = base_color * (input.light_color + input.ambient_color);
    
	// Add spectacular silver lining (sun_intensity already in vertex lighting, don't double it)
	lit_color += sun_color * silver_lining * 0.3;

	// Subsurface scattering glow at edges (reduce intensity)
	float3 sss_color = sun_color * edge * 0.1 * saturate(sun_facing + 0.5);
	lit_color += sss_color;
    
    // Ambient occlusion in dense cores
    float ao = 1.0 - density * 0.3;
    lit_color *= ao;
    
    // Slight blue tint in shadows for atmosphere
    float shadow_amount = 1.0 - self_shadow_factor;
    lit_color = lerp(lit_color, lit_color * float3(0.9, 0.95, 1.1), shadow_amount * 0.3);
    
    // HDR bloom hint - allow slight overbright
    lit_color = min(lit_color, 2.0);
    
    // Premultiplied alpha output
    return float4(lit_color * final_alpha, final_alpha);
}

// -----------------------------------------------------------------------------
// FOG PASS (when inside cloud)
// -----------------------------------------------------------------------------

struct FogVS_Output
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

FogVS_Output cloud_fog_vs(uint vertex_id : SV_VertexID)
{
    FogVS_Output output;
    
    // Fullscreen triangle
    output.uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0.5, 1);
    
    return output;
}

float4 cloud_fog_ps(FogVS_Output input) : SV_Target0
{
    // Simple fog overlay when camera is inside a cloud
    // This would be enhanced in Tier 3 with proper raymarching
    
    float2 noise_uv = input.uv * noise_scale * 0.5 + cloud_time * noise_animation_speed * 0.1;
    float noise = sample_bf3_noise(noise_uv, 0);
    
    float fog_density = noise * 0.3; // Subtle fog effect
    
    float3 fog_color = lerp(cloud_color_dark, cloud_color_bright, 0.5);
    fog_color *= ambient_intensity;
    
    return float4(fog_color * fog_density, fog_density);
}

// -----------------------------------------------------------------------------
// DEPTH-AWARE UPSAMPLE PASS
// -----------------------------------------------------------------------------
// Composites low-res cloud buffer onto full-res scene with depth-aware filtering
// to avoid bleeding clouds over foreground geometry edges.

// Low-res cloud buffer (rendered at 1/4 resolution)
Texture2D<float4> lowres_clouds : register(t0);

// Full-res scene depth for edge detection (world geometry)
Texture2D<float> fullres_depth : register(t1);

// First-person depth buffer for occlusion
Texture2D<float> firstperson_depth : register(t2);

cbuffer UpsampleConstants : register(b2)
{
    float2 lowres_size;      // Low-res buffer dimensions
    float2 fullres_size;     // Full-res buffer dimensions
    float2 texel_size;       // 1.0 / lowres_size
    float depth_threshold;   // Depth difference threshold for edge detection
    float upsample_sharpness; // How sharp the upsampling is (higher = sharper edges)
    float2 upsample_depth_params;  // For linearizing depth in upsample pass
    float use_firstperson_depth;   // 1.0 if first-person depth is available
    float _pad;
};

struct UpsampleVS_Output
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

UpsampleVS_Output cloud_upsample_vs(uint vertex_id : SV_VertexID)
{
    UpsampleVS_Output output;
    
    // Fullscreen triangle
    output.uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0.5, 1);
    
    return output;
}

float4 cloud_upsample_ps(UpsampleVS_Output input) : SV_Target0
{
    // Simple bilinear upsample from low-res buffer
    float4 result = lowres_clouds.SampleLevel(sampler_linear_clamp, input.uv, 0);
    
    // Early out if fully transparent
    if (result.a < 0.001)
        discard;
    
    // Check first-person depth occlusion
    if (use_firstperson_depth > 0.5)
    {
        float fp_raw_depth = firstperson_depth.SampleLevel(sampler_point_clamp, input.uv, 0);
        
        // If first-person has been drawn here (depth < 1.0), hide the clouds
        // First-person depth buffer is cleared to 1.0, so any value less than that
        // means first-person geometry is present
        if (fp_raw_depth < 0.9999)
        {
            discard;
        }
    }
    
    return result;
}

// -----------------------------------------------------------------------------
// OIT UPSAMPLE PASS - Submits upsampled clouds to OIT for proper transparency sorting
// -----------------------------------------------------------------------------

#include "adaptive_oit.hlsl"

void cloud_upsample_oit_ps(UpsampleVS_Output input)
{
    // Simple bilinear upsample from low-res buffer
    float4 result = lowres_clouds.SampleLevel(sampler_linear_clamp, input.uv, 0);
    
    // Early out if fully transparent
    if (result.a < 0.001)
    {
        discard;
    }
    
    // Check first-person depth occlusion
    if (use_firstperson_depth > 0.5)
    {
        float fp_raw_depth = firstperson_depth.SampleLevel(sampler_point_clamp, input.uv, 0);
        
        // If first-person has been drawn here (depth < 1.0), hide the clouds
        if (fp_raw_depth < 0.9999)
        {
            discard;
        }
    }
    
    // Sample scene depth at this pixel for OIT sorting
    float scene_z = fullres_depth.SampleLevel(sampler_point_clamp, input.uv, 0);
    
    // Convert from premultiplied alpha back to straight alpha for OIT
    float4 straight_color = float4(result.rgb / max(result.a, 0.001), result.a);
    
    // Write to OIT - use the scene depth at this pixel as the cloud's depth
    aoit::write_pixel((uint2)input.position.xy, scene_z, straight_color, 1);
}

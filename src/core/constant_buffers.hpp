#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

namespace sp::core::cb {

// Evil macro. Takes the type of the constant buffer and the first non-game
// constant in the type and returns the number of game constants in the buffer.
#define CB_MAX_GAME_CONSTANTS(Type, first_patch_constant)                      \
   (sizeof(Type) - (sizeof(Type) - offsetof(Type, first_patch_constant))) /    \
      sizeof(glm::vec4);

struct Scene_tag {};
struct Draw_tag {};
struct Fixedfunction_tag {};
struct Skin_tag {};
struct Draw_ps_tag {};

static constexpr Scene_tag scene{};
static constexpr Draw_tag draw{};
static constexpr Fixedfunction_tag fixedfunction{};
static constexpr Skin_tag skin{};
static constexpr Draw_ps_tag draw_ps{};

struct alignas(16) Scene {
   std::array<glm::vec4, 4> projection_matrix;
   glm::vec3 vs_view_positionWS;
   float _padding0;
   glm::vec4 fog_info;
   float near_scene_fade_scale;
   float near_scene_fade_offset;
   float vs_lighting_scale;
   std::uint32_t _padding1{};
   std::array<glm::vec4, 3> shadow_map_transform;
   glm::vec2 pixel_offset;
   std::uint32_t input_color_srgb;
   std::uint32_t vs_use_soft_skinning;
   float time;
   float prev_near_scene_fade_scale;
   float prev_near_scene_fade_offset;
};

constexpr auto scene_game_count = CB_MAX_GAME_CONSTANTS(Scene, pixel_offset);

static_assert(sizeof(Scene) == 192);
static_assert(scene_game_count == 10);

struct alignas(16) Draw {
   glm::vec3 normaltex_decompress;
   std::uint32_t compressed_position;
   glm::vec3 position_decompress_min;
   std::uint32_t compressed_texcoords;
   glm::vec4 position_decompress_max;
   glm::vec4 color_state;
   std::array<glm::vec4, 3> world_matrix;
   glm::vec4 light_ambient_color_top;
   glm::vec4 light_ambient_color_bottom;
   glm::vec4 light_directional_0_color;
   glm::vec4 light_directional_0_dir;
   glm::vec4 light_directional_1_color;
   glm::vec4 light_directional_1_dir;
   glm::vec4 light_point_0_color;
   glm::vec4 light_point_0_pos;
   glm::vec4 light_point_1_color;
   glm::vec4 light_point_1_pos;
   glm::vec4 overlapping_lights[4];
   glm::vec4 light_proj_color;
   glm::vec4 light_proj_selector;
   std::array<glm::vec4, 4> light_proj_matrix;
   glm::vec4 material_diffuse_color;
   glm::vec4 custom_constants[9];
};

constexpr auto draw_game_count = sizeof(Draw) / 16;

static_assert(sizeof(Draw) == 592);
static_assert(draw_game_count == 37);

struct alignas(16) Fixedfunction {
   glm::vec4 texture_factor;
   glm::vec2 inv_resolution;
   std::array<float, 2> _buffer_padding;
};

static_assert(sizeof(Fixedfunction) == 32);

struct alignas(16) Skin {
   std::array<std::array<glm::vec4, 3>, 15> bone_matrices;
};

constexpr auto skin_game_count = sizeof(Skin) / 16;

static_assert(sizeof(Skin) == 720);
static_assert(skin_game_count == 45);

struct alignas(16) Draw_ps {
   std::array<glm::vec4, 5> ps_custom_constants;
   glm::vec3 ps_view_positionWS;
   float ps_lighting_scale;
   glm::vec4 rt_resolution; // x = width, y = height, z = 1 / width, w = 1 / height
   glm::vec3 fog_color;
   std::uint32_t light_active = 0;
   std::uint32_t light_active_point_count = 0;
   std::uint32_t light_active_spot = 0;
   std::uint32_t additive_blending;
   std::uint32_t cube_projtex;
   std::uint32_t fog_enabled;
   std::uint32_t limit_normal_shader_bright_lights;
   std::uint32_t input_color_srgb;
   std::uint32_t supersample_alpha_test;
   std::uint32_t ssao_enabled;
   float time_seconds;
   std::array<uint32_t, 1> padding;
};

constexpr auto draw_ps_game_count = CB_MAX_GAME_CONSTANTS(Draw_ps, ps_view_positionWS);

static_assert(sizeof(Draw_ps) == 176);
static_assert(draw_ps_game_count == 5);

#undef CB_MAX_GAME_CONSTANTS

struct alignas(16) Team_colors {
   alignas(16) glm::vec3 friend_color;
   alignas(16) glm::vec3 friend_health_color;
   alignas(16) glm::vec3 friend_corsshair_dot_color;
   alignas(16) glm::vec3 foe_color;
   alignas(16) glm::vec3 foe_text_color;
   alignas(16) glm::vec3 foe_text_alt_color;
   alignas(16) glm::vec3 foe_health_color;
   alignas(16) glm::vec3 foe_flag_color;
   alignas(16) glm::vec3 foe_crosshair_dot_color;
};

static_assert(sizeof(Team_colors) == 144);

// SWBF3-style post-process fog constants
// Based on SceneVolumeData template parameters (1:1 mapping)
struct alignas(16) Fog {
   // Inverse view matrix for world position reconstruction (64 bytes)
   glm::mat4 inv_view_matrix;

   // Distance fog - SWBF3: fog[], fogNear, fogFar
   glm::vec4 fog_color{1.0f, 1.0f, 1.0f, 0.0f};  // fog[0-3]: RGB + intensity/alpha
   float fog_start{0.0f};                         // fogNear
   float fog_end{80.0f};                          // fogFar

   // Height/Atmospheric fog - SWBF3: fogMinHeight, fogMaxHeight, fogDensity, fogAlpha
   float height_base{0.0f};                       // fogMinHeight
   float height_ceiling{250.0f};                  // fogMaxHeight
   float atmos_density{0.012f};                   // fogDensity (atmosdata.x)
   float fog_alpha{0.0f};                         // fogAlpha (atmosdata.z) - min atmosphere for above-layer

   // Projection params for view-space reconstruction
   float proj_scale_x{1.0f};                      // projection[0][0]
   float proj_scale_y{1.0f};                      // projection[1][1]

   // Camera info (must be 16-byte aligned for HLSL)
   glm::vec3 camera_position{0.0f, 0.0f, 0.0f};
   float time{0.0f};

   // Options - SWBF3: fogAdd, fogSky
   std::uint32_t blend_additive{0};               // fogAdd: 0=lerp, 1=additive
   std::uint32_t apply_to_sky{1};                 // fogSky: apply fog to sky (default true per SWBF3)

   // Depth linearization params (extracted from projection matrix)
   glm::vec2 depth_linearize_params{1.0f, 1.0f};

   // Height falloff: blend rate to min atmosphere for above-layer rays (atmosdata.w)
   float height_falloff{1.0f};

   // Immersion: adds near-field fog when camera is in fog layer (0 = off, 1 = full)
   float fog_immersion{0.0f};

   // Immersion distance curve: start fade-in, full strength, fade-out end
   float immersion_start{0.0f};    // Distance where immersion fog starts fading in
   float immersion_end{20.0f};     // Distance where immersion fog reaches full strength
   float immersion_range{240.0f};  // Distance where immersion fog fades out to 0

   // Height fog distance range option: 0 = ignore fog_start/end, 1 = respect them
   std::uint32_t height_fog_use_distance_range{0};

   // Ceiling fade: distance above ceiling where fog smoothly fades to 0
   float ceiling_fade{0.0f};

   // Fog disc boundary: limits fog to a circular area in XZ
   float fog_disc_center_x{0.0f};
   float fog_disc_center_z{0.0f};
   float fog_disc_radius{0.0f};      // 0 = disabled (infinite fog)
   float fog_disc_edge_fade{50.0f};  // Fade distance at disc edge
};

static_assert(sizeof(Fog) == 192);

// Per-layer cloud parameters for SWBF3-style 3-layer cloud system
struct alignas(16) CloudLayerParams {
   // Lit cloud color + cover
   glm::vec3 light_color{1.0f, 0.87f, 0.66f};
   float cover{0.45f};            // cloudLayerCover: cloud coverage threshold

   // Dark/shadow cloud color + sharpness
   glm::vec3 dark_color{0.0f, 0.0f, 0.0f};
   float sharpness{0.9f};         // cloudLayerSharpness: edge softness

   // Noise octave weights (8 octaves across two vec4s)
   glm::vec4 octave_weights_0to3{0.9f, 0.4f, 0.25f, 0.125f};
   glm::vec4 octave_weights_4to7{0.08f, 0.06f, 0.04f, 0.02f};

   // Noise octave evolution frequencies (animation speed per octave)
   glm::vec4 octave_evol_freqs_0to3{0.005f, 0.015f, 0.028f, 0.05f};
   glm::vec4 octave_evol_freqs_4to7{0.16f, 0.32f, 0.64f, 1.28f};

   // Geometry: altitude, curved_radius, plane_size, tiling_scale
   float altitude{1600.0f};       // cloudLayerPlaneAltitude
   float curved_radius{30000.0f}; // cloudLayerCurvedPlaneRadius
   float plane_size{5.0f};        // cloudLayerPlaneSizeScale (multiplied by 2000)
   float tiling_scale{1.5f};      // cloudLayerNoiseTexTilingScale

   // Animation: wind_speed, wind_angle, half_height, lightray_step
   float wind_speed{0.002f};      // cloudLayerWindSpeed
   float wind_angle{90.0f};       // cloudLayerWindAngleFromXAxis (degrees)
   float half_height{300.0f};     // cloudLayerHalfHeight
   float lightray_step{0.5f};     // cloudLayerLightrayStepLength

   // Lighting: max_lighting, min_lighting, scattering, enabled
   float max_lighting{1.0f};      // cloudLayerMaxLighting
   float min_lighting{0.0f};      // cloudLayerMinLighting
   float scattering{0.06f};       // cloudLayerLightScattering
   std::uint32_t enabled{0};      // Per-layer enable flag

   // World-space plane center (clouds are fixed in world, not camera-relative)
   float plane_center_x{0.0f};
   float plane_center_z{0.0f};
   float _padding0{0.0f};
   float _padding1{0.0f};

   // Preset factory functions
   static CloudLayerParams stratus() noexcept
   {
      CloudLayerParams p;
      // Stratus: Low, flat, uniform gray layer clouds
      p.light_color = {0.85f, 0.85f, 0.88f};  // Slightly blue-gray
      p.dark_color = {0.4f, 0.42f, 0.45f};    // Darker gray undersides
      p.cover = 0.65f;                         // High coverage, uniform
      p.sharpness = 0.4f;                      // Soft, diffuse edges
      p.altitude = 600.0f;                     // Low altitude
      p.curved_radius = 30000.0f;              // Gentle curvature
      p.plane_size = 8.0f;                     // Large extent
      p.tiling_scale = 0.8f;                   // Large features
      p.wind_speed = 0.001f;                   // Slow movement
      p.wind_angle = 75.0f;
      p.half_height = 150.0f;
      p.lightray_step = 0.3f;
      p.scattering = 0.08f;                    // More scattering (denser)
      p.max_lighting = 0.9f;
      p.min_lighting = 0.15f;                  // Never too dark
      p.octave_weights_0to3 = {1.0f, 0.3f, 0.15f, 0.08f};  // Emphasize low frequency
      p.octave_weights_4to7 = {0.04f, 0.02f, 0.01f, 0.005f};
      return p;
   }

   static CloudLayerParams cumulus() noexcept
   {
      CloudLayerParams p;
      // Cumulus: Mid-level, puffy, well-defined clouds
      p.light_color = {1.0f, 0.95f, 0.88f};   // Bright white with warm tint
      p.dark_color = {0.25f, 0.28f, 0.35f};   // Blue-gray shadows
      p.cover = 0.45f;                         // Moderate coverage
      p.sharpness = 1.2f;                      // Sharp, defined edges
      p.altitude = 1400.0f;                    // Mid altitude
      p.curved_radius = 30000.0f;
      p.plane_size = 6.0f;
      p.tiling_scale = 1.5f;                   // Medium detail
      p.wind_speed = 0.002f;                   // Moderate speed
      p.wind_angle = 90.0f;
      p.half_height = 400.0f;
      p.lightray_step = 0.5f;
      p.scattering = 0.06f;
      p.max_lighting = 1.0f;
      p.min_lighting = 0.05f;
      p.octave_weights_0to3 = {0.9f, 0.5f, 0.3f, 0.18f};   // Balanced frequencies
      p.octave_weights_4to7 = {0.1f, 0.06f, 0.03f, 0.015f};
      return p;
   }

   static CloudLayerParams cirrus() noexcept
   {
      CloudLayerParams p;
      // Cirrus: High altitude, wispy ice crystal clouds
      p.light_color = {1.0f, 0.98f, 1.0f};    // Bright white, slight cool tint
      p.dark_color = {0.6f, 0.65f, 0.75f};    // Very light shadows (thin)
      p.cover = 0.3f;                          // Sparse coverage
      p.sharpness = 0.25f;                     // Very soft, wispy edges
      p.altitude = 3500.0f;                    // High altitude
      p.curved_radius = 30000.0f;              // Gentle curvature
      p.plane_size = 10.0f;                    // Very large extent
      p.tiling_scale = 2.5f;                   // Fine detail/streaks
      p.wind_speed = 0.004f;                   // Fast (jet stream)
      p.wind_angle = 110.0f;                   // Different wind direction
      p.half_height = 200.0f;
      p.lightray_step = 0.2f;                  // Short steps (thin layer)
      p.scattering = 0.02f;                    // Low scattering (thin)
      p.max_lighting = 1.0f;
      p.min_lighting = 0.4f;                   // Always bright (thin, translucent)
      p.octave_weights_0to3 = {0.6f, 0.4f, 0.35f, 0.25f};  // More high frequency
      p.octave_weights_4to7 = {0.15f, 0.12f, 0.08f, 0.04f};
      p.octave_evol_freqs_0to3 = {0.008f, 0.02f, 0.04f, 0.08f};  // Faster evolution
      p.octave_evol_freqs_4to7 = {0.2f, 0.4f, 0.8f, 1.6f};
      return p;
   }
};

static_assert(sizeof(CloudLayerParams) == 160);

// SWBF3 cloud layers constant buffer
// Supports 3 independent cloud layers rendered back-to-front
// Default layers: [0] Stratus (low), [1] Cumulus (mid), [2] Cirrus (high)
struct alignas(16) CloudLayers {
   // Inverse view matrix for world position reconstruction (64 bytes)
   glm::mat4 inv_view_matrix;

   // Camera info
   glm::vec3 camera_position{0.0f, 0.0f, 0.0f};
   float time{0.0f};

   // Sun direction (normalized, pointing toward sun) + layer count
   glm::vec3 sun_direction{0.0f, -1.0f, 0.0f};
   std::uint32_t layer_count{3};  // Active layer count

   // Projection params for view-space reconstruction + depth linearization
   float proj_scale_x{1.0f};
   float proj_scale_y{1.0f};
   glm::vec2 depth_linearize_params{1.0f, 1.0f};

   // 3 cloud layers with preset defaults: Stratus, Cumulus, Cirrus
   std::array<CloudLayerParams, 3> layers = {{
      CloudLayerParams::stratus(),
      CloudLayerParams::cumulus(),
      CloudLayerParams::cirrus()
   }};
};

static_assert(sizeof(CloudLayers) == 592);

// Legacy alias for backward compatibility
using CloudLayer = CloudLayerParams;

// SWBF3-style volumetric cloud area parameters
// Used for scattered 3D cloud volumes within a bounding region
struct alignas(16) CloudVolumes {
   // Bounding box for cloud placement (world space)
   glm::vec3 area_min{-5000.0f, 800.0f, -5000.0f};
   float sharpness{0.85f};

   glm::vec3 area_max{5000.0f, 2500.0f, 5000.0f};
   float light_scattering{3.0f};

   // Cloud size ranges
   glm::vec3 cloud_size_min{800.0f, 400.0f, 800.0f};
   float max_lighting{1.0f};

   glm::vec3 cloud_size_max{2000.0f, 800.0f, 2000.0f};
   float min_lighting{0.0f};

   // Colors
   glm::vec3 light_color{1.0f, 0.87f, 0.66f};
   float noise_influence{0.7f};

   glm::vec3 dark_color{0.2f, 0.22f, 0.28f};
   float noise_tiling{1.5f};

   // Sun direction
   glm::vec3 sun_direction{0.577f, -0.577f, 0.577f};
   std::uint32_t cloud_count{60};

   // Depth fade parameters
   float depth_fade_near{20.0f};
   float depth_fade_far{400.0f};
   float edge_softness{0.3f};
   float density{1.0f};

   // Animation
   float wind_speed{0.01f};
   float wind_angle{45.0f};
   float evolution_speed{0.1f};
   std::uint32_t seed{12345};
};

static_assert(sizeof(CloudVolumes) == 144);

}

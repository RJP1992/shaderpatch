#pragma once

#include "../core/texture_database.hpp"
#include "../shader/database.hpp"
#include "com_ptr.hpp"
#include "glm_yaml_adapters.hpp"
#include "profiler.hpp"

#include <array>

#include <d3d11_4.h>
#include <glm/glm.hpp>

#pragma warning(push)
#pragma warning(disable : 4996)
#pragma warning(disable : 4127)

#include <yaml-cpp/yaml.h>
#pragma warning(pop)

namespace sp::effects {

struct Cloud_layer {
   bool enabled = true;
   float height = 500.0f;           // World Y coordinate at center
   float thickness = 50.0f;         // Vertical extent for fog transition
   float scale = 0.0003f;           // UV scale for noise
   float density = 0.8f;            // Visual opacity
   float scroll_speed = 0.01f;      // Wind speed (very slow default)
   float scroll_angle = 45.0f;      // Wind direction in degrees
   float curvature = 0.0f;          // 0 = flat, positive = curves down toward horizon

   // Per-layer appearance
   float cloud_threshold = 0.4f;    // Noise threshold for cloud formation
   float cloud_softness = 0.3f;     // Edge softness
   float sun_color_influence = 0.5f; // How much sun color affects this layer
   glm::vec3 color_lit = {1.0f, 1.0f, 1.0f};
   glm::vec3 color_dark = {0.6f, 0.65f, 0.7f};
   float lighting_wrap = 0.3f;
   float cloud_brightness = 1.0f;
   float min_brightness = 0.3f;

   // BF3-style octave weights (RGBA channels)
   glm::vec4 octave_weights = {0.5f, 0.3f, 0.15f, 0.05f};  // R=large, G=medium, B=fine, A=edge
   glm::vec4 octave_blend = {0.5f, 0.5f, 0.5f, 0.5f};      // Blend between primary/secondary sample

   // Lighting mode
   bool use_normal_lighting = false;  // false = BF3 style, true = normal map lighting

   // Fog integration
   float fog_boost_max = 1.0f;      // 0 = no fog, 1 = full whiteout when inside
};

struct Cloud_params {
   bool enabled = false;

   std::array<Cloud_layer, 3> layers = {{
      // Layer 0: Low, dense clouds - emphasize large shapes
      {true, 300.0f, 60.0f, 0.0003f, 0.9f, 0.01f, 45.0f, 0.0f,
       0.4f, 0.3f, 0.5f, {1.0f, 1.0f, 1.0f}, {0.6f, 0.65f, 0.7f}, 0.3f, 1.0f, 0.3f,
       {0.5f, 0.3f, 0.15f, 0.05f}, {0.5f, 0.5f, 0.5f, 0.5f}, false, 1.0f},
      // Layer 1: Mid clouds - balanced
      {true, 500.0f, 50.0f, 0.0004f, 0.7f, 0.015f, 50.0f, 0.0f,
       0.45f, 0.35f, 0.5f, {1.0f, 1.0f, 1.0f}, {0.65f, 0.7f, 0.75f}, 0.35f, 1.0f, 0.35f,
       {0.4f, 0.35f, 0.2f, 0.05f}, {0.5f, 0.5f, 0.5f, 0.5f}, false, 0.6f},
      // Layer 2: High, wispy clouds - more detail
      {true, 800.0f, 40.0f, 0.0005f, 0.5f, 0.02f, 55.0f, 0.0f,
       0.5f, 0.4f, 0.5f, {1.0f, 1.0f, 1.0f}, {0.7f, 0.75f, 0.8f}, 0.4f, 1.0f, 0.4f,
       {0.3f, 0.3f, 0.25f, 0.15f}, {0.5f, 0.5f, 0.5f, 0.5f}, false, 0.3f},
   }};

   // Fading (shared)
   float horizon_fade_start = 0.15f;
   float horizon_fade_end = 0.02f;
   float distance_fade_start = 8000.0f;
   float distance_fade_end = 15000.0f;

   // Near camera fade
   float near_fade_start = 100.0f;
   float near_fade_end = 10.0f;

   // Fog integration
   float global_fog_boost_scale = 1.0f;

   // Curvature center (world XZ coordinates)
   glm::vec2 curvature_center = {0.0f, 0.0f};
};

struct Cloud_input {
   glm::mat4 view_matrix;
   glm::mat4 projection_matrix;
   glm::vec3 camera_position;
   glm::vec3 sun_direction;
   glm::vec3 sun_color;
   float time;
   UINT width;
   UINT height;
   ID3D11ShaderResourceView* depth_near;
   ID3D11ShaderResourceView* depth_far;
};

class Clouds {
public:
   Clouds(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept;

   void render(ID3D11DeviceContext1& dc,
               Profiler& profiler,
               const core::Shader_resource_database& textures,
               ID3D11RenderTargetView& output_rtv,
               const Cloud_input& input) noexcept;

   [[nodiscard]] float calculate_fog_boost(float camera_y) const noexcept;

   void params(const Cloud_params& params) noexcept { _params = params; }
   [[nodiscard]] auto params() const noexcept -> const Cloud_params& { return _params; }

private:
   // GPU constant buffer structure (must match HLSL)
   struct alignas(16) Gpu_cloud_layer {
      float height;
      float thickness;
      float scale;
      float density;

      float scroll_speed;
      float scroll_angle;
      float fog_boost_max;
      float curvature;

      float cloud_threshold;
      float cloud_softness;
      float sun_color_influence;
      float lighting_wrap;

      glm::vec3 color_lit;
      float cloud_brightness;

      glm::vec3 color_dark;
      float min_brightness;

      glm::vec4 octave_weights;     // BF3-style RGBA channel weights
      glm::vec4 octave_blend;       // Blend between primary/secondary sample

      float use_normal_lighting;    // 0.0 = BF3 style, 1.0 = normal map
      glm::vec3 _pad;
   };
   static_assert(sizeof(Gpu_cloud_layer) == 128);

   struct alignas(16) Constants {
      glm::mat4 inverse_view_projection;  // 64 bytes

      glm::vec3 camera_position;          // 12 bytes
      float time;                         // 4 bytes

      glm::vec3 sun_direction;            // 12 bytes
      float horizon_fade_start;           // 4 bytes

      glm::vec3 sun_color;                // 12 bytes
      float horizon_fade_end;             // 4 bytes

      float distance_fade_start;          // 4 bytes
      float distance_fade_end;            // 4 bytes
      float near_fade_start;              // 4 bytes
      float near_fade_end;                // 4 bytes

      glm::vec2 curvature_center;         // 8 bytes
      glm::vec2 _pad;                     // 8 bytes

      Gpu_cloud_layer layers[3];          // 384 bytes (128 * 3)
   };
   static_assert(sizeof(Constants) == 528);

   [[nodiscard]] auto pack_constants(const Cloud_params& params,
                                     const Cloud_input& input) const noexcept -> Constants;

   Cloud_params _params{};

   Com_ptr<ID3D11Device5> _device;
   Com_ptr<ID3D11Buffer> _constant_buffer;
   Com_ptr<ID3D11VertexShader> _vs;
   Com_ptr<ID3D11PixelShader> _ps;
   Com_ptr<ID3D11BlendState> _blend_state;
   Com_ptr<ID3D11DepthStencilState> _no_depth_state;
   Com_ptr<ID3D11SamplerState> _aniso_sampler;

   // Cloud octave texture (RGBA = 4 noise octaves)
   Com_ptr<ID3D11ShaderResourceView> _cloud_octaves_srv;
};

}

namespace YAML {

template<>
struct convert<sp::effects::Cloud_layer> {
   static Node encode(const sp::effects::Cloud_layer& layer)
   {
      using namespace std::literals;

      YAML::Node node;

      node["Enable"s] = layer.enabled;
      node["Height"s] = layer.height;
      node["Thickness"s] = layer.thickness;
      node["Scale"s] = layer.scale;
      node["Density"s] = layer.density;
      node["ScrollSpeed"s] = layer.scroll_speed;
      node["ScrollAngle"s] = layer.scroll_angle;
      node["Curvature"s] = layer.curvature;

      node["Threshold"s] = layer.cloud_threshold;
      node["Softness"s] = layer.cloud_softness;
      node["SunColorInfluence"s] = layer.sun_color_influence;
      node["ColorLit"s] = layer.color_lit;
      node["ColorDark"s] = layer.color_dark;
      node["LightingWrap"s] = layer.lighting_wrap;
      node["CloudBrightness"s] = layer.cloud_brightness;
      node["MinBrightness"s] = layer.min_brightness;

      node["OctaveWeights"s] = layer.octave_weights;
      node["OctaveBlend"s] = layer.octave_blend;
      node["UseNormalLighting"s] = layer.use_normal_lighting;

      node["FogBoostMax"s] = layer.fog_boost_max;

      return node;
   }

   static bool decode(const Node& node, sp::effects::Cloud_layer& layer)
   {
      using namespace std::literals;

      layer = sp::effects::Cloud_layer{};

      layer.enabled = node["Enable"s].as<bool>(layer.enabled);
      layer.height = node["Height"s].as<float>(layer.height);
      layer.thickness = node["Thickness"s].as<float>(layer.thickness);
      layer.scale = node["Scale"s].as<float>(layer.scale);
      layer.density = node["Density"s].as<float>(layer.density);
      layer.scroll_speed = node["ScrollSpeed"s].as<float>(layer.scroll_speed);
      layer.scroll_angle = node["ScrollAngle"s].as<float>(layer.scroll_angle);
      layer.curvature = node["Curvature"s].as<float>(layer.curvature);

      layer.cloud_threshold = node["Threshold"s].as<float>(layer.cloud_threshold);
      layer.cloud_softness = node["Softness"s].as<float>(layer.cloud_softness);
      layer.sun_color_influence = node["SunColorInfluence"s].as<float>(layer.sun_color_influence);
      layer.color_lit = node["ColorLit"s].as<glm::vec3>(layer.color_lit);
      layer.color_dark = node["ColorDark"s].as<glm::vec3>(layer.color_dark);
      layer.lighting_wrap = node["LightingWrap"s].as<float>(layer.lighting_wrap);
      layer.cloud_brightness = node["CloudBrightness"s].as<float>(layer.cloud_brightness);
      layer.min_brightness = node["MinBrightness"s].as<float>(layer.min_brightness);

      layer.octave_weights = node["OctaveWeights"s].as<glm::vec4>(layer.octave_weights);
      layer.octave_blend = node["OctaveBlend"s].as<glm::vec4>(layer.octave_blend);
      layer.use_normal_lighting = node["UseNormalLighting"s].as<bool>(layer.use_normal_lighting);

      layer.fog_boost_max = node["FogBoostMax"s].as<float>(layer.fog_boost_max);

      return true;
   }
};

template<>
struct convert<sp::effects::Cloud_params> {
   static Node encode(const sp::effects::Cloud_params& params)
   {
      using namespace std::literals;

      YAML::Node node;

      node["Enable"s] = params.enabled;
      node["GlobalFogBoostScale"s] = params.global_fog_boost_scale;

      node["Layer0"s] = params.layers[0];
      node["Layer1"s] = params.layers[1];
      node["Layer2"s] = params.layers[2];

      node["HorizonFadeStart"s] = params.horizon_fade_start;
      node["HorizonFadeEnd"s] = params.horizon_fade_end;
      node["DistanceFadeStart"s] = params.distance_fade_start;
      node["DistanceFadeEnd"s] = params.distance_fade_end;
      node["NearFadeStart"s] = params.near_fade_start;
      node["NearFadeEnd"s] = params.near_fade_end;
      node["CurvatureCenter"s] = params.curvature_center;

      return node;
   }

   static bool decode(const Node& node, sp::effects::Cloud_params& params)
   {
      using namespace std::literals;

      params = sp::effects::Cloud_params{};

      params.enabled = node["Enable"s].as<bool>(params.enabled);
      params.global_fog_boost_scale = node["GlobalFogBoostScale"s].as<float>(params.global_fog_boost_scale);

      params.layers[0] = node["Layer0"s].as<sp::effects::Cloud_layer>(params.layers[0]);
      params.layers[1] = node["Layer1"s].as<sp::effects::Cloud_layer>(params.layers[1]);
      params.layers[2] = node["Layer2"s].as<sp::effects::Cloud_layer>(params.layers[2]);

      params.horizon_fade_start = node["HorizonFadeStart"s].as<float>(params.horizon_fade_start);
      params.horizon_fade_end = node["HorizonFadeEnd"s].as<float>(params.horizon_fade_end);
      params.distance_fade_start = node["DistanceFadeStart"s].as<float>(params.distance_fade_start);
      params.distance_fade_end = node["DistanceFadeEnd"s].as<float>(params.distance_fade_end);
      params.near_fade_start = node["NearFadeStart"s].as<float>(params.near_fade_start);
      params.near_fade_end = node["NearFadeEnd"s].as<float>(params.near_fade_end);
      params.curvature_center = node["CurvatureCenter"s].as<glm::vec2>(params.curvature_center);

      return true;
   }
};

}


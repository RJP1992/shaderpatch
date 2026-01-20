
#include "control.hpp"
#include "../imgui/imgui_ext.hpp"
#include "../logger.hpp"
#include "../user_config.hpp"
#include "file_dialogs.hpp"
#include "filmic_tonemapper.hpp"
#include "postprocess_params.hpp"
#include "tonemappers.hpp"
#include "volume_resource.hpp"

#include <fstream>
#include <functional>
#include <numbers>
#include <random>
#include <sstream>
#include <string_view>

#include <fmt/format.h>

#include "../imgui/imgui.h"

namespace YAML {
template<>
struct convert<sp::core::cb::Fog> {
   static Node encode(const sp::core::cb::Fog& params)
   {
      using namespace std::literals;

      YAML::Node node;

      node["FogColor"s] = std::vector<float>{params.fog_color.x, params.fog_color.y,
                                              params.fog_color.z, params.fog_color.w};
      node["FogStart"s] = params.fog_start;
      node["FogEnd"s] = params.fog_end;
      node["HeightBase"s] = params.height_base;
      node["HeightCeiling"s] = params.height_ceiling;
      node["AtmosDensity"s] = params.atmos_density;
      node["FogAlpha"s] = params.fog_alpha;
      node["BlendAdditive"s] = (params.blend_additive != 0);
      node["ApplyToSky"s] = (params.apply_to_sky != 0);
      node["HeightFalloff"s] = params.height_falloff;
      node["FogImmersion"s] = params.fog_immersion;
      node["ImmersionStart"s] = params.immersion_start;
      node["ImmersionEnd"s] = params.immersion_end;
      node["ImmersionRange"s] = params.immersion_range;
      node["HeightFogUseDistanceRange"s] = (params.height_fog_use_distance_range != 0);
      node["CeilingFade"s] = params.ceiling_fade;
      node["DiscCenterX"s] = params.fog_disc_center_x;
      node["DiscCenterZ"s] = params.fog_disc_center_z;
      node["DiscRadius"s] = params.fog_disc_radius;
      node["DiscEdgeFade"s] = params.fog_disc_edge_fade;

      return node;
   }

   static bool decode(const Node& node, sp::core::cb::Fog& params)
   {
      using namespace std::literals;

      params = sp::core::cb::Fog{};

      if (auto color = node["FogColor"s]) {
         auto c = color.as<std::vector<float>>(std::vector<float>{1.0f, 1.0f, 1.0f, 0.0f});
         if (c.size() >= 4) params.fog_color = {c[0], c[1], c[2], c[3]};
      }
      params.fog_start = node["FogStart"s].as<float>(params.fog_start);
      params.fog_end = node["FogEnd"s].as<float>(params.fog_end);
      params.height_base = node["HeightBase"s].as<float>(params.height_base);
      params.height_ceiling = node["HeightCeiling"s].as<float>(params.height_ceiling);
      params.atmos_density = node["AtmosDensity"s].as<float>(params.atmos_density);
      params.fog_alpha = node["FogAlpha"s].as<float>(params.fog_alpha);
      params.blend_additive = node["BlendAdditive"s].as<bool>(false) ? 1 : 0;
      params.apply_to_sky = node["ApplyToSky"s].as<bool>(true) ? 1 : 0;
      params.height_falloff = node["HeightFalloff"s].as<float>(params.height_falloff);
      params.fog_immersion = node["FogImmersion"s].as<float>(params.fog_immersion);
      params.immersion_start = node["ImmersionStart"s].as<float>(params.immersion_start);
      params.immersion_end = node["ImmersionEnd"s].as<float>(params.immersion_end);
      params.immersion_range = node["ImmersionRange"s].as<float>(params.immersion_range);
      params.height_fog_use_distance_range =
         node["HeightFogUseDistanceRange"s].as<bool>(false) ? 1 : 0;
      params.ceiling_fade = node["CeilingFade"s].as<float>(params.ceiling_fade);
      params.fog_disc_center_x = node["DiscCenterX"s].as<float>(params.fog_disc_center_x);
      params.fog_disc_center_z = node["DiscCenterZ"s].as<float>(params.fog_disc_center_z);
      params.fog_disc_radius = node["DiscRadius"s].as<float>(params.fog_disc_radius);
      params.fog_disc_edge_fade = node["DiscEdgeFade"s].as<float>(params.fog_disc_edge_fade);

      return true;
   }
};

template<>
struct convert<sp::core::cb::CloudLayerParams> {
   static Node encode(const sp::core::cb::CloudLayerParams& params)
   {
      using namespace std::literals;

      YAML::Node node;

      node["Enabled"s] = (params.enabled != 0);
      node["Cover"s] = params.cover;
      node["Sharpness"s] = params.sharpness;
      node["Scattering"s] = params.scattering;
      node["LightColor"s] = std::vector<float>{params.light_color.x, params.light_color.y, params.light_color.z};
      node["DarkColor"s] = std::vector<float>{params.dark_color.x, params.dark_color.y, params.dark_color.z};
      node["OctaveWeights0to3"s] = std::vector<float>{params.octave_weights_0to3.x, params.octave_weights_0to3.y, params.octave_weights_0to3.z, params.octave_weights_0to3.w};
      node["OctaveWeights4to7"s] = std::vector<float>{params.octave_weights_4to7.x, params.octave_weights_4to7.y, params.octave_weights_4to7.z, params.octave_weights_4to7.w};
      node["OctaveEvolFreqs0to3"s] = std::vector<float>{params.octave_evol_freqs_0to3.x, params.octave_evol_freqs_0to3.y, params.octave_evol_freqs_0to3.z, params.octave_evol_freqs_0to3.w};
      node["OctaveEvolFreqs4to7"s] = std::vector<float>{params.octave_evol_freqs_4to7.x, params.octave_evol_freqs_4to7.y, params.octave_evol_freqs_4to7.z, params.octave_evol_freqs_4to7.w};
      node["Altitude"s] = params.altitude;
      node["CurvedRadius"s] = params.curved_radius;
      node["PlaneSize"s] = params.plane_size;
      node["TilingScale"s] = params.tiling_scale;
      node["WindSpeed"s] = params.wind_speed;
      node["WindAngle"s] = params.wind_angle;
      node["HalfHeight"s] = params.half_height;
      node["LightrayStep"s] = params.lightray_step;
      node["MaxLighting"s] = params.max_lighting;
      node["MinLighting"s] = params.min_lighting;
      node["PlaneCenterX"s] = params.plane_center_x;
      node["PlaneCenterZ"s] = params.plane_center_z;

      return node;
   }

   static bool decode(const Node& node, sp::core::cb::CloudLayerParams& params)
   {
      using namespace std::literals;

      params = sp::core::cb::CloudLayerParams{};

      params.enabled = node["Enabled"s].as<bool>(false) ? 1 : 0;
      params.cover = node["Cover"s].as<float>(params.cover);
      params.sharpness = node["Sharpness"s].as<float>(params.sharpness);
      params.scattering = node["Scattering"s].as<float>(params.scattering);

      if (auto c = node["LightColor"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{1.0f, 0.87f, 0.66f});
         if (v.size() >= 3) params.light_color = {v[0], v[1], v[2]};
      }
      if (auto c = node["DarkColor"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{0.0f, 0.0f, 0.0f});
         if (v.size() >= 3) params.dark_color = {v[0], v[1], v[2]};
      }
      if (auto c = node["OctaveWeights0to3"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{0.9f, 0.4f, 0.25f, 0.125f});
         if (v.size() >= 4) params.octave_weights_0to3 = {v[0], v[1], v[2], v[3]};
      }
      if (auto c = node["OctaveWeights4to7"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{0.08f, 0.06f, 0.04f, 0.02f});
         if (v.size() >= 4) params.octave_weights_4to7 = {v[0], v[1], v[2], v[3]};
      }
      if (auto c = node["OctaveEvolFreqs0to3"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{0.005f, 0.015f, 0.028f, 0.05f});
         if (v.size() >= 4) params.octave_evol_freqs_0to3 = {v[0], v[1], v[2], v[3]};
      }
      if (auto c = node["OctaveEvolFreqs4to7"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{0.16f, 0.32f, 0.64f, 1.28f});
         if (v.size() >= 4) params.octave_evol_freqs_4to7 = {v[0], v[1], v[2], v[3]};
      }

      params.altitude = node["Altitude"s].as<float>(params.altitude);
      params.curved_radius = node["CurvedRadius"s].as<float>(params.curved_radius);
      params.plane_size = node["PlaneSize"s].as<float>(params.plane_size);
      params.tiling_scale = node["TilingScale"s].as<float>(params.tiling_scale);
      params.wind_speed = node["WindSpeed"s].as<float>(params.wind_speed);
      params.wind_angle = node["WindAngle"s].as<float>(params.wind_angle);
      params.half_height = node["HalfHeight"s].as<float>(params.half_height);
      params.lightray_step = node["LightrayStep"s].as<float>(params.lightray_step);
      params.max_lighting = node["MaxLighting"s].as<float>(params.max_lighting);
      params.min_lighting = node["MinLighting"s].as<float>(params.min_lighting);
      params.plane_center_x = node["PlaneCenterX"s].as<float>(params.plane_center_x);
      params.plane_center_z = node["PlaneCenterZ"s].as<float>(params.plane_center_z);

      return true;
   }
};

template<>
struct convert<sp::core::cb::CloudLayers> {
   static Node encode(const sp::core::cb::CloudLayers& params)
   {
      using namespace std::literals;

      YAML::Node node;

      node["SunDirection"s] = std::vector<float>{params.sun_direction.x, params.sun_direction.y, params.sun_direction.z};

      YAML::Node layers_node;
      for (int i = 0; i < 3; i++) {
         layers_node.push_back(convert<sp::core::cb::CloudLayerParams>::encode(params.layers[i]));
      }
      node["Layers"s] = layers_node;

      return node;
   }

   static bool decode(const Node& node, sp::core::cb::CloudLayers& params)
   {
      using namespace std::literals;

      params = sp::core::cb::CloudLayers{};

      if (auto c = node["SunDirection"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{0.0f, -1.0f, 0.0f});
         if (v.size() >= 3) params.sun_direction = {v[0], v[1], v[2]};
      }

      if (auto layers_node = node["Layers"s]) {
         for (size_t i = 0; i < layers_node.size() && i < 3; i++) {
            params.layers[i] = layers_node[i].as<sp::core::cb::CloudLayerParams>(sp::core::cb::CloudLayerParams{});
         }
      }

      return true;
   }
};

template<>
struct convert<sp::core::cb::CloudVolumes> {
   static Node encode(const sp::core::cb::CloudVolumes& params)
   {
      using namespace std::literals;

      YAML::Node node;

      node["AreaMin"s] = std::vector<float>{params.area_min.x, params.area_min.y, params.area_min.z};
      node["AreaMax"s] = std::vector<float>{params.area_max.x, params.area_max.y, params.area_max.z};
      node["CloudSizeMin"s] = std::vector<float>{params.cloud_size_min.x, params.cloud_size_min.y, params.cloud_size_min.z};
      node["CloudSizeMax"s] = std::vector<float>{params.cloud_size_max.x, params.cloud_size_max.y, params.cloud_size_max.z};
      node["LightColor"s] = std::vector<float>{params.light_color.x, params.light_color.y, params.light_color.z};
      node["DarkColor"s] = std::vector<float>{params.dark_color.x, params.dark_color.y, params.dark_color.z};
      node["SunDirection"s] = std::vector<float>{params.sun_direction.x, params.sun_direction.y, params.sun_direction.z};
      node["CloudCount"s] = params.cloud_count;
      node["Sharpness"s] = params.sharpness;
      node["LightScattering"s] = params.light_scattering;
      node["MaxLighting"s] = params.max_lighting;
      node["MinLighting"s] = params.min_lighting;
      node["NoiseInfluence"s] = params.noise_influence;
      node["NoiseTiling"s] = params.noise_tiling;
      node["DepthFadeNear"s] = params.depth_fade_near;
      node["DepthFadeFar"s] = params.depth_fade_far;
      node["EdgeSoftness"s] = params.edge_softness;
      node["Density"s] = params.density;
      node["WindSpeed"s] = params.wind_speed;
      node["WindAngle"s] = params.wind_angle;
      node["EvolutionSpeed"s] = params.evolution_speed;
      node["Seed"s] = params.seed;

      return node;
   }

   static bool decode(const Node& node, sp::core::cb::CloudVolumes& params)
   {
      using namespace std::literals;

      params = sp::core::cb::CloudVolumes{};

      if (auto c = node["AreaMin"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{-5000.0f, 800.0f, -5000.0f});
         if (v.size() >= 3) params.area_min = {v[0], v[1], v[2]};
      }
      if (auto c = node["AreaMax"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{5000.0f, 2500.0f, 5000.0f});
         if (v.size() >= 3) params.area_max = {v[0], v[1], v[2]};
      }
      if (auto c = node["CloudSizeMin"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{800.0f, 400.0f, 800.0f});
         if (v.size() >= 3) params.cloud_size_min = {v[0], v[1], v[2]};
      }
      if (auto c = node["CloudSizeMax"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{2000.0f, 800.0f, 2000.0f});
         if (v.size() >= 3) params.cloud_size_max = {v[0], v[1], v[2]};
      }
      if (auto c = node["LightColor"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{1.0f, 0.87f, 0.66f});
         if (v.size() >= 3) params.light_color = {v[0], v[1], v[2]};
      }
      if (auto c = node["DarkColor"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{0.2f, 0.22f, 0.28f});
         if (v.size() >= 3) params.dark_color = {v[0], v[1], v[2]};
      }
      if (auto c = node["SunDirection"s]) {
         auto v = c.as<std::vector<float>>(std::vector<float>{0.577f, -0.577f, 0.577f});
         if (v.size() >= 3) params.sun_direction = {v[0], v[1], v[2]};
      }

      params.cloud_count = node["CloudCount"s].as<std::uint32_t>(params.cloud_count);
      params.sharpness = node["Sharpness"s].as<float>(params.sharpness);
      params.light_scattering = node["LightScattering"s].as<float>(params.light_scattering);
      params.max_lighting = node["MaxLighting"s].as<float>(params.max_lighting);
      params.min_lighting = node["MinLighting"s].as<float>(params.min_lighting);
      params.noise_influence = node["NoiseInfluence"s].as<float>(params.noise_influence);
      params.noise_tiling = node["NoiseTiling"s].as<float>(params.noise_tiling);
      params.depth_fade_near = node["DepthFadeNear"s].as<float>(params.depth_fade_near);
      params.depth_fade_far = node["DepthFadeFar"s].as<float>(params.depth_fade_far);
      params.edge_softness = node["EdgeSoftness"s].as<float>(params.edge_softness);
      params.density = node["Density"s].as<float>(params.density);
      params.wind_speed = node["WindSpeed"s].as<float>(params.wind_speed);
      params.wind_angle = node["WindAngle"s].as<float>(params.wind_angle);
      params.evolution_speed = node["EvolutionSpeed"s].as<float>(params.evolution_speed);
      params.seed = node["Seed"s].as<std::uint32_t>(params.seed);

      return true;
   }
};
}

namespace sp::effects {

using namespace std::literals;
namespace fs = std::filesystem;

namespace {

constexpr std::wstring_view auto_user_config_name = L"shader patch.spfx";

Bloom_params show_bloom_imgui(Bloom_params params) noexcept;

Vignette_params show_vignette_imgui(Vignette_params params) noexcept;

Color_grading_params show_color_grading_imgui(Color_grading_params params) noexcept;

Color_grading_params show_tonemapping_imgui(Color_grading_params params) noexcept;

Film_grain_params show_film_grain_imgui(Film_grain_params params) noexcept;

DOF_params show_dof_imgui(DOF_params params) noexcept;

SSAO_params show_ssao_imgui(SSAO_params params) noexcept;

FFX_cas_params show_ffx_cas_imgui(FFX_cas_params params) noexcept;

void show_fog_imgui(Postprocess_fog& fog) noexcept;

void show_cloud_layer_imgui(Cloud_layer& cloud) noexcept;

void show_cloud_volume_imgui(Cloud_volume& cloud) noexcept;

void show_tonemapping_curve(std::function<float(float)> tonemapper) noexcept;
}

Control::Control(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept
   : postprocess{device, shaders},
     cmaa2{device, shaders.compute("CMAA2"sv)},
     ssao{device, shaders},
     ffx_cas{device, shaders},
     mask_nan{device, shaders},
     postprocess_fog{device, shaders},
     cloud_layer{device, shaders},
     cloud_volume{device, shaders},
     profiler{device}
{
   if (user_config.graphics.enable_user_effects_config) {
      load_params_from_yaml_file(user_config.graphics.user_effects_config);
   }
   else if (user_config.graphics.enable_user_effects_auto_config) {
      try {
         _has_auto_user_config =
            std::filesystem::exists(auto_user_config_name) &&
            std::filesystem::is_regular_file(auto_user_config_name);

         if (_has_auto_user_config) {
            load_params_from_yaml_file(auto_user_config_name);
         }
      }
      catch (std::exception&) {
         _has_auto_user_config = false;
      }
   }
}

bool Control::enabled(const bool enable) noexcept
{
   _enabled = enable;

   if (!_enabled && user_config.graphics.enable_user_effects_config)
      load_params_from_yaml_file(user_config.graphics.user_effects_config);
   else if (!_enabled && _has_auto_user_config)
      load_params_from_yaml_file(auto_user_config_name);

   return enabled();
}

bool Control::enabled() const noexcept
{
   const bool enable_user_effects_auto_config =
      _has_auto_user_config && user_config.graphics.enable_user_effects_auto_config;

   return (_enabled || user_config.graphics.enable_user_effects_config ||
           enable_user_effects_auto_config);
}

bool Control::allow_scene_blur() const noexcept
{
   if (!enabled()) return true;

   const Bloom_params& params = postprocess.bloom_params();

   return !(enabled() && params.mode == Bloom_mode::blended);
}

void Control::show_imgui(HWND game_window) noexcept
{
   ImGui::SetNextWindowSize({533, 591}, ImGuiCond_FirstUseEver);
   ImGui::Begin("Effects", nullptr);

   if (ImGui::BeginTabBar("Effects Config")) {
      if (ImGui::BeginTabItem("Control")) {
         ImGui::BeginDisabled(enabled() && !_enabled);

         ImGui::Checkbox("Enable Effects", &_enabled);

         ImGui::EndDisabled();

         if (enabled() && !_enabled) {
            ImGui::Text("Effects are being enabled from the user config.");
         }

         if (ImGui::CollapsingHeader("Effects Config", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("HDR Rendering", &_config.hdr_rendering);

            if (ImGui::IsItemHovered()) {
               ImGui::SetTooltip(
                  "HDR rendering works best with custom materials "
                  "and may give poor results without them.");
            }

            ImGui::Checkbox("Request Order-Independent Transparency",
                            &_config.oit_requested);

            if (ImGui::IsItemHovered()) {
               ImGui::SetTooltip(
                  "Informs SP that OIT is required for some models to "
                  "render correctly and that it should be enabled if the "
                  "user's GPU supports it.");
            }

            ImGui::Checkbox("Request Soft Skinning", &_config.soft_skinning_requested);

            if (ImGui::IsItemHovered()) {
               ImGui::SetTooltip(
                  "Informs SP that soft skinning is required for some models "
                  "to render correctly and that it should be enabled even if "
                  "the "
                  "user has switched it off.");
            }

            if (!_config.hdr_rendering) {
               ImGui::Checkbox("Floating-point Render Targets",
                               &_config.fp_rendertargets);

               if (ImGui::IsItemHovered()) {
                  ImGui::SetTooltip("Controls usage of floating-point "
                                    "rendertargets, which can preserve more "
                                    "color detail in the bright areas of the "
                                    "scene for when Bloom is applied.");
               }

               ImGui::Checkbox("Disable Light Brightness Rescaling",
                               &_config.disable_light_brightness_rescaling);

               if (ImGui::IsItemHovered()) {
                  ImGui::SetTooltip(
                     "Disable light brightness rescaling in stock shaders. Has "
                     "no affect on custom materials. HDR Rendering implies "
                     "this option.");
               }
            }

            if (_config.hdr_rendering || _config.fp_rendertargets) {
               ImGui::Checkbox("Bugged Cloth Workaround",
                               &_config.workaround_bugged_cloth);

               if (ImGui::IsItemHovered()) {
                  ImGui::SetTooltip(
                     "Some cloth can produce NaNs when drawn. These turn into "
                     "large black boxes when ran through the bloom filter. "
                     "This option enables a pass to convert these NaNs into "
                     "pure black pixels.\n\nThis option should not be "
                     "prohbitively expensive but it is always cheaper to not "
                     "run it if it is not needed.\n\nIf you're a modder always "
                     "try and fix your cloth assets first before enabling "
                     "this.");
               }
            }
         }

         ImGui::Checkbox("Profiler Enabled", &profiler.enabled);

         ImGui::Separator();

         imgui_save_widget(game_window);

         ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Post Processing")) {
         show_post_processing_imgui();

         ImGui::Separator();

         imgui_save_widget(game_window);

         ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Color Grading Regions")) {
         postprocess.show_color_grading_regions_imgui(game_window,
                                                      &show_color_grading_imgui,
                                                      &show_bloom_imgui);

         ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
   }

   ImGui::Separator();

   ImGui::End();

   config_changed();
}

void Control::read_config(YAML::Node config)
{
   this->config(
      config["Control"s].as<Effects_control_config>(Effects_control_config{}));
   postprocess.color_grading_params(
      config["ColorGrading"s].as<Color_grading_params>(Color_grading_params{}));
   postprocess.bloom_params(config["Bloom"s].as<Bloom_params>(Bloom_params{}));
   postprocess.vignette_params(
      config["Vignette"s].as<Vignette_params>(Vignette_params{}));
   postprocess.film_grain_params(
      config["FilmGrain"s].as<Film_grain_params>(Film_grain_params{}));
   postprocess.dof_params(config["DOF"s].as<DOF_params>(DOF_params{}));
   ssao.params(config["SSAO"s].as<SSAO_params>(SSAO_params{false}));
   ffx_cas.params(config["ContrastAdaptiveSharpening"s].as<FFX_cas_params>(
      FFX_cas_params{false}));

   if (auto fog_node = config["Fog"s]) {
      postprocess_fog.fog_params() = fog_node.as<core::cb::Fog>(core::cb::Fog{});
      postprocess_fog.enabled(fog_node["Enable"s].as<bool>(false));
   }

   if (auto cloud_node = config["CloudLayers"s]) {
      cloud_layer.params() = cloud_node.as<core::cb::CloudLayers>(core::cb::CloudLayers{});
      cloud_layer.enabled(cloud_node["Enable"s].as<bool>(false));
   }
   // Legacy single-layer format support
   else if (auto legacy_cloud_node = config["CloudLayer"s]) {
      auto legacy_params = legacy_cloud_node.as<core::cb::CloudLayerParams>(core::cb::CloudLayerParams{});
      legacy_params.enabled = legacy_cloud_node["Enable"s].as<bool>(false) ? 1 : 0;
      cloud_layer.params().layers[0] = legacy_params;
      cloud_layer.enabled(legacy_params.enabled != 0);
   }

   if (auto volume_node = config["CloudVolumes"s]) {
      cloud_volume.params() = volume_node.as<core::cb::CloudVolumes>(core::cb::CloudVolumes{});
      cloud_volume.enabled(volume_node["Enable"s].as<bool>(false));
      cloud_volume.regenerate_clouds();
   }
}

auto Control::output_params_to_yaml_string() noexcept -> std::string
{
   YAML::Node config;

   config["Control"s] = _config;
   config["ColorGrading"s] = postprocess.color_grading_params();
   config["Bloom"s] = postprocess.bloom_params();
   config["Vignette"s] = postprocess.vignette_params();
   config["FilmGrain"s] = postprocess.film_grain_params();
   config["DOF"s] = postprocess.dof_params();
   config["SSAO"s] = ssao.params();
   config["ContrastAdaptiveSharpening"s] = ffx_cas.params();

   config["Fog"s] = postprocess_fog.fog_params();
   config["Fog"s]["Enable"s] = postprocess_fog.enabled();

   config["CloudLayers"s] = cloud_layer.params();
   config["CloudLayers"s]["Enable"s] = cloud_layer.enabled();

   config["CloudVolumes"s] = cloud_volume.params();
   config["CloudVolumes"s]["Enable"s] = cloud_volume.enabled();

   std::stringstream stream;

   stream << "# Auto-Generated Effects Config. May have less than ideal formatting.\n"sv;

   stream << config;

   return stream.str();
}

void Control::save_params_to_yaml_file(const fs::path& save_to) noexcept
{
   auto config = output_params_to_yaml_string();

   std::ofstream file{save_to};

   if (!file) {
      log(Log_level::error, "Failed to open file "sv, save_to,
          " to write effects config."sv);

      _save_failure = true;

      return;
   }

   file << config;

   _save_failure = false;
}

void Control::save_params_to_munged_file(const fs::path& save_to) noexcept
{
   auto config = output_params_to_yaml_string();

   try {
      save_volume_resource(save_to, save_to.stem().string(),
                           Volume_resource_type::fx_config,
                           std::span{reinterpret_cast<std::byte*>(config.data()),
                                     config.size()});
      _save_failure = false;
   }
   catch (std::exception& e) {
      log(Log_level::warning, "Exception occured while writing effects config tp "sv,
          save_to, " Reason: "sv, e.what());

      _save_failure = true;
   }
}

void Control::load_params_from_yaml_file(const fs::path& load_from) noexcept
{
   YAML::Node config;

   try {
      config = YAML::LoadFile(load_from.string());
   }
   catch (std::exception& e) {
      log(Log_level::error, "Failed to open file "sv, load_from,
          " to read effects config. Reason: "sv, e.what());

      _open_failure = true;
   }

   try {
      read_config(config);
   }
   catch (std::exception& e) {
      log(Log_level::warning, "Exception occured while reading effects config from "sv,
          load_from, " Reason: "sv, e.what());

      _open_failure = true;
   }

   _open_failure = false;
}

void Control::imgui_save_widget(const HWND game_window) noexcept
{
   if (ImGui::Button("Open Config")) {
      if (auto path = win32::open_file_dialog({{L"Effects Config", L"*.spfx"}},
                                              game_window, fs::current_path(),
                                              L"mod_config.spfx"s);
          path) {
         load_params_from_yaml_file(*path);
      }
   }

   if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Open a previously saved config.");
   }

   if (_open_failure) {
      ImGui::SameLine();
      ImGui::TextColored({1.0f, 0.2f, 0.33f, 1.0f}, "Open Failed!");
   }

   ImGui::SameLine();

   if (ImGui::Button("Save Config")) {
      if (auto path = win32::save_file_dialog({{L"Effects Config", L"*.spfx"}},
                                              game_window, fs::current_path(),
                                              L"mod_config.spfx"s);
          path) {
         save_params_to_yaml_file(*path);
      }
   }

   if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Save out a config to be passed to `spfx_munge` or "
                        "loaded back up from the developer screen.");
   }

   ImGui::SameLine();

   if (ImGui::Button("Save Munged Config")) {
      if (auto path =
             win32::save_file_dialog({{L"Munged Effects Config", L"*.mspfx"}}, game_window,
                                     fs::current_path(), L"mod_config.mspfx"s);
          path) {
         save_params_to_munged_file(*path);
      }
   }

   if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
         "Save out a munged config to be loaded from a map script. Keep "
         "in "
         "mind Shader "
         "Patch can not reload these files from the developer screen.");
   }

   if (_save_failure) {
      ImGui::SameLine();
      ImGui::TextColored({1.0f, 0.2f, 0.33f, 1.0f}, "Save Failed!");
   }
}

void Control::show_post_processing_imgui() noexcept
{
   if (ImGui::BeginTabBar("Post Processing", ImGuiTabBarFlags_None)) {
      if (ImGui::BeginTabItem("Color Grading")) {
         postprocess.color_grading_params(
            show_color_grading_imgui(postprocess.color_grading_params()));

         ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Tonemapping")) {
         postprocess.color_grading_params(
            show_tonemapping_imgui(postprocess.color_grading_params()));

         ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Bloom")) {
         postprocess.bloom_params(show_bloom_imgui(postprocess.bloom_params()));

         ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Vignette")) {
         postprocess.vignette_params(
            show_vignette_imgui(postprocess.vignette_params()));

         ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Film Grain")) {
         postprocess.film_grain_params(
            show_film_grain_imgui(postprocess.film_grain_params()));

         ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Depth of Field")) {
         postprocess.dof_params(show_dof_imgui(postprocess.dof_params()));

         ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("SSAO")) {
         ssao.params(show_ssao_imgui(ssao.params()));

         ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Contrast Adaptive Sharpening")) {
         ffx_cas.params(show_ffx_cas_imgui(ffx_cas.params()));

         ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Fog")) {
         show_fog_imgui(postprocess_fog);

         ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Cloud Layer")) {
         show_cloud_layer_imgui(cloud_layer);

         ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Cloud Volumes")) {
         show_cloud_volume_imgui(cloud_volume);

         ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
   }
}

void Control::config_changed() noexcept
{
   postprocess.hdr_state(_config.hdr_rendering ? Hdr_state::hdr : Hdr_state::stock);
}

namespace {

Bloom_params show_bloom_imgui(Bloom_params params) noexcept
{
   if (ImGui::CollapsingHeader("Basic Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("Enabled", &params.enabled);

      const ImVec2 pre_mode_cursor = ImGui::GetCursorPos();

      if (ImGui::RadioButton("Blended", params.mode == Bloom_mode::blended)) {
         params.mode = Bloom_mode::blended;
      }

      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "New blended bloom mode. It doesn't require configuring a "
            "threshold and is easier to work with.\n\nHowever it can result in "
            "everything having a very slightly \"softer\" look to it, "
            "depending on how high the Blend Factor is.");
      }

      ImGui::SameLine();

      if (ImGui::RadioButton("Threshold##Mode", params.mode == Bloom_mode::threshold)) {
         params.mode = Bloom_mode::threshold;
      }

      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Classic threshold bloom mode. What has been in SP "
            "since v1.0, it can give nice results as well but "
            "can require more tweaking to get right.\n\nHowever if you don't "
            "like the softness of the blended mode and lowering the Blend "
            "Factor doesn't help but you still want bloom this mode is likely "
            "what you want.");
      }

      ImGui::SetCursorPos(pre_mode_cursor);

      ImGui::LabelText("Mode", "");

      if (params.mode == Bloom_mode::blended)
         ImGui::DragFloat("Blend Factor", &params.blend_factor, 0.025f);
      else
         ImGui::DragFloat("Threshold##Param", &params.threshold, 0.025f);

      params.blend_factor = std::clamp(params.blend_factor, 0.0f, 1.0f);
      params.threshold = std::clamp(params.threshold, 0.0f, 1.0f);

      ImGui::DragFloat("Intensity", &params.intensity, 0.025f, 0.0f,
                       std::numeric_limits<float>::max());
      ImGui::ColorEdit3("Tint", &params.tint.x, ImGuiColorEditFlags_Float);
   }

   if (ImGui::CollapsingHeader("Individual Scales & Tints")) {
      ImGui::DragFloat("Inner Scale", &params.inner_scale, 0.025f, 0.0f,
                       std::numeric_limits<float>::max());
      ImGui::DragFloat("Inner Mid Scale", &params.inner_mid_scale, 0.025f, 0.0f,
                       std::numeric_limits<float>::max());
      ImGui::DragFloat("Mid Scale", &params.mid_scale, 0.025f, 0.0f,
                       std::numeric_limits<float>::max());
      ImGui::DragFloat("Outer Mid Scale", &params.outer_mid_scale, 0.025f, 0.0f,
                       std::numeric_limits<float>::max());
      ImGui::DragFloat("Outer Scale", &params.outer_scale, 0.025f, 0.0f,
                       std::numeric_limits<float>::max());

      ImGui::ColorEdit3("Inner Tint", &params.inner_tint.x, ImGuiColorEditFlags_Float);
      ImGui::ColorEdit3("Inner Mid Tint", &params.inner_mid_tint.x,
                        ImGuiColorEditFlags_Float);
      ImGui::ColorEdit3("Mid Tint", &params.mid_tint.x, ImGuiColorEditFlags_Float);
      ImGui::ColorEdit3("Outer Mid Tint", &params.outer_mid_tint.x,
                        ImGuiColorEditFlags_Float);
      ImGui::ColorEdit3("Outer Tint", &params.outer_tint.x, ImGuiColorEditFlags_Float);
   }

   if (ImGui::CollapsingHeader("Dirt")) {
      ImGui::Checkbox("Use Dirt", &params.use_dirt);
      ImGui::DragFloat("Dirt Scale", &params.dirt_scale, 0.025f, 0.0f,
                       std::numeric_limits<float>::max());
      ImGui::ColorEdit3("Dirt Tint", &params.dirt_tint.x, ImGuiColorEditFlags_Float);

      ImGui::InputText("Dirt Texture", params.dirt_texture_name);
   }

   ImGui::Separator();

   if (ImGui::Button("Reset Settings")) params = Bloom_params{};

   if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Reset bloom params to default settings.");
   }

   return params;
}

Vignette_params show_vignette_imgui(Vignette_params params) noexcept
{
   ImGui::Checkbox("Enabled", &params.enabled);

   ImGui::DragFloat("End", &params.end, 0.05f, 0.0f, 2.0f);
   ImGui::DragFloat("Start", &params.start, 0.05f, 0.0f, 2.0f);

   return params;
}

Color_grading_params show_color_grading_imgui(Color_grading_params params) noexcept
{
   if (ImGui::CollapsingHeader("Basic Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::ColorEdit3("Colour Filter", &params.color_filter.x,
                        ImGuiColorEditFlags_Float);
      ImGui::DragFloat("Exposure", &params.exposure, 0.01f);
      ImGui::DragFloat("Brightness", &params.brightness, 0.01f);
      ImGui::DragFloat("Saturation", &params.saturation, 0.01f, 0.0f, 5.0f);
      ImGui::DragFloat("Contrast", &params.contrast, 0.01f, 0.01f, 5.0f);
   }

   if (ImGui::CollapsingHeader("Lift / Gamma / Gain")) {
      ImGui::ColorEdit3("Shadow Colour", &params.shadow_color.x,
                        ImGuiColorEditFlags_Float);
      ImGui::DragFloat("Shadow Offset", &params.shadow_offset, 0.005f);

      ImGui::ColorEdit3("Midtone Colour", &params.midtone_color.x,
                        ImGuiColorEditFlags_Float);
      ImGui::DragFloat("Midtone Offset", &params.midtone_offset, 0.005f);

      ImGui::ColorEdit3("Hightlight Colour", &params.highlight_color.x,
                        ImGuiColorEditFlags_Float);
      ImGui::DragFloat("Hightlight Offset", &params.highlight_offset, 0.005f);
   }

   if (ImGui::CollapsingHeader("Channel Mixer")) {
      ImGui::DragFloatFormatted3("Red", &params.channel_mix_red.x,
                                 {"R: %.3f", "G: %.3f", "B: %.3f"}, 0.025f,
                                 -2.0f, 2.0f);
      ImGui::DragFloatFormatted3("Green", &params.channel_mix_green.x,
                                 {"R: %.3f", "G: %.3f", "B: %.3f"}, 0.025f,
                                 -2.0f, 2.0f);
      ImGui::DragFloatFormatted3("Blue", &params.channel_mix_blue.x,
                                 {"R: %.3f", "G: %.3f", "B: %.3f"}, 0.025f,
                                 -2.0f, 2.0f);
   }

   if (ImGui::CollapsingHeader("Hue / Saturation / Value")) {
      params.hsv_hue_adjustment *= 360.0f;

      ImGui::DragFloat("Hue Adjustment", &params.hsv_hue_adjustment, 1.0f,
                       -180.0f, 180.0f);

      ImGui::DragFloat("Saturation Adjustment",
                       &params.hsv_saturation_adjustment, 0.025f, 0.0f, 2.0f);

      ImGui::DragFloat("Value Adjustment", &params.hsv_value_adjustment, 0.025f,
                       0.0f, 2.0f);

      params.hsv_hue_adjustment /= 360.0f;
   }

   ImGui::Separator();

   if (ImGui::Button("Reset Settings")) params = Color_grading_params{};

   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Will also reset Tonemapping settings.");

   return params;
}

Color_grading_params show_tonemapping_imgui(Color_grading_params params) noexcept
{
   params.tonemapper = tonemapper_from_string(ImGui::StringPicker(
      "Tonemapper", std::string{to_string(params.tonemapper)},
      std::initializer_list<std::string>{to_string(Tonemapper::filmic),
                                         to_string(Tonemapper::aces_fitted),
                                         to_string(Tonemapper::filmic_heji2015),
                                         to_string(Tonemapper::reinhard),
                                         to_string(Tonemapper::none)}));

   if (params.tonemapper == Tonemapper::filmic) {
      show_tonemapping_curve([curve = filmic::color_grading_params_to_curve(params)](
                                const float v) { return filmic::eval(v, curve); });

      ImGui::DragFloat("Toe Strength", &params.filmic_toe_strength, 0.01f, 0.0f, 1.0f);
      ImGui::DragFloat("Toe Length", &params.filmic_toe_length, 0.01f, 0.0f, 1.0f);
      ImGui::DragFloat("Shoulder Strength", &params.filmic_shoulder_strength,
                       0.01f, 0.0f, 100.0f);
      ImGui::DragFloat("Shoulder Length", &params.filmic_shoulder_length, 0.01f,
                       0.0f, 1.0f);
      ImGui::DragFloat("Shoulder Angle", &params.filmic_shoulder_angle, 0.01f,
                       0.0f, 1.0f);

      if (ImGui::Button("Reset to Linear")) {
         params.filmic_toe_strength = 0.0f;
         params.filmic_toe_length = 0.5f;
         params.filmic_shoulder_strength = 0.0f;
         params.filmic_shoulder_length = 0.5f;
         params.filmic_shoulder_angle = 0.0f;
      }

      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Reset the curve to linear values.");

      ImGui::SameLine();

      if (ImGui::Button("Load Example Starting Point")) {
         params.filmic_toe_strength = 0.5f;
         params.filmic_toe_length = 0.5f;
         params.filmic_shoulder_strength = 2.0f;
         params.filmic_shoulder_length = 0.5f;
         params.filmic_shoulder_angle = 1.0f;
      }
   }
   else if (params.tonemapper == Tonemapper::aces_fitted) {
      show_tonemapping_curve(
         [](const float v) { return eval_aces_srgb_fitted(glm::vec3{v}).r; });
   }
   else if (params.tonemapper == Tonemapper::filmic_heji2015) {
      show_tonemapping_curve([whitepoint = params.filmic_heji_whitepoint](const float v) {
         return eval_filmic_hejl2015(glm::vec3{v}, whitepoint).x;
      });

      ImGui::DragFloat("Whitepoint", &params.filmic_heji_whitepoint, 0.01f);
   }
   else if (params.tonemapper == Tonemapper::reinhard) {
      show_tonemapping_curve(
         [](const float v) { return eval_reinhard(glm::vec3{v}).x; });
   }

   return params;
}

Film_grain_params show_film_grain_imgui(Film_grain_params params) noexcept
{
   ImGui::Checkbox("Enabled", &params.enabled);
   ImGui::Checkbox("Colored", &params.colored);

   ImGui::DragFloat("Amount", &params.amount, 0.001f, 0.0f, 1.0f);
   ImGui::DragFloat("Size", &params.size, 0.05f, 1.6f, 3.0f);
   ImGui::DragFloat("Color Amount", &params.color_amount, 0.05f, 0.0f, 1.0f);
   ImGui::DragFloat("Luma Amount", &params.luma_amount, 0.05f, 0.0f, 1.0f);

   return params;
}

DOF_params show_dof_imgui(DOF_params params) noexcept
{
   ImGui::Checkbox("Enabled", &params.enabled);

   ImGui::DragFloat("Film Size", &params.film_size_mm, 1.0f, 1.0f, 256.0f);

   ImGui::SetItemTooltip(
      "Film/Sensor Size for the Depth of Field. Due to limitations in how "
      "Shader Patch works this does not alter the FOV.");

   ImGui::DragFloat("Focus Distance", &params.focus_distance, 1.0f, 0.0f, 1e10f);

   ImGui::SetItemTooltip("Distance to the plane in focus.");

   int f_stop_index = static_cast<int>(std::round(
      std::log(double{params.f_stop}) / std::log(std::numbers::sqrt2_v<double>)));

   if (ImGui::SliderInt("f-stop", &f_stop_index, 0, 10,
                        fmt::format("f/{:.1f}", params.f_stop).c_str(),
                        ImGuiSliderFlags_NoInput)) {
      params.f_stop = static_cast<float>(
         std::pow(std::numbers::sqrt2_v<double>, static_cast<double>(f_stop_index)));
   }

   ImGui::SetItemTooltip(
      "f-stop/f-number for the lens. Higher numbers create "
      "less blur, lower numbers cause more blur. This does not currently alter "
      "the exposure either as it would on a real lens.");

   ImGui::InputFloat("f-stop##raw", &params.f_stop, 1.0f, 1.0f, "f/%.1f");

   ImGui::SetItemTooltip("Manual input for the f-stop.");

   ImGui::Separator();

   ImGui::TextWrapped(
      "Focal Length is controlled by ingame FOV.\n\nBe aware as well that the "
      "current Depth of Field implementation can interact poorly with "
      "transparent surfaces/particles and also the far scene.");

   params.film_size_mm = std::max(params.film_size_mm, 1.0f);
   params.focus_distance = std::max(params.focus_distance, 0.0f);
   params.f_stop = std::max(params.f_stop, 1.0f);

   return params;
}

SSAO_params show_ssao_imgui(SSAO_params params) noexcept
{
   ImGui::Checkbox("Enabled", &params.enabled);

   const ImVec2 pre_mode_cursor = ImGui::GetCursorPos();

   if (ImGui::RadioButton("Ambient", params.mode == SSAO_mode::ambient)) {
      params.mode = SSAO_mode::ambient;
   }

   if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
         "SSAO will affect ambient and vertex lighting only. This is more "
         "accurate. It produces a sublte effect in direct lighting and a more "
         "pronounced effect in shadows.");
   }

   ImGui::SameLine();

   if (ImGui::RadioButton("Global", params.mode == SSAO_mode::global)) {
      params.mode = SSAO_mode::global;
   }

   if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
         "SSAO will affect all lighting. This is less accurate "
         "and was the default before the Ambient mode was added.");
   }

   ImGui::SetCursorPos(pre_mode_cursor);

   ImGui::LabelText("Mode", "");

   ImGui::DragFloat("Radius", &params.radius, 0.1f, 0.1f, 2.0f);
   ImGui::DragFloat("Shadow Multiplier", &params.shadow_multiplier, 0.05f, 0.0f, 5.0f);
   ImGui::DragFloat("Shadow Power", &params.shadow_power, 0.05f, 0.0f, 5.0f);
   ImGui::DragFloat("Detail Shadow Strength", &params.detail_shadow_strength,
                    0.05f, 0.0f, 5.0f);
   ImGui::DragInt("Blur Amount", &params.blur_pass_count, 0.25f, 0, 6);
   ImGui::DragFloat("Sharpness", &params.sharpness, 0.01f, 0.0f, 1.0f);

   return params;
}

FFX_cas_params show_ffx_cas_imgui(FFX_cas_params params) noexcept
{
   ImGui::Checkbox("Enabled", &params.enabled);

   ImGui::DragFloat("Sharpness", &params.sharpness, 0.01f, 0.0f, 1.0f);

   params.sharpness = std::clamp(params.sharpness, 0.0f, 1.0f);

   return params;
}

void show_tonemapping_curve(std::function<float(float)> tonemapper) noexcept
{
   static float divisor = 256.0f;
   static int index_count = 1024;

   const auto plotter = [](void* data, int idx) {
      const auto& tonemapper = *static_cast<std::function<float(float)>*>(data);

      float v = idx / divisor;

      return tonemapper(v);
   };

   static int range = 0;

   ImGui::PlotLines("Tonemap Curve", plotter, &tonemapper, index_count);
   ImGui::SliderInt("Curve Preview Range", &range, 0, 4);

   ImGui::SameLine();

   switch (range) {
   case 0:
      divisor = 256.0f;
      index_count = 1024;
      ImGui::TextUnformatted("0.0 to 4.0");
      break;
   case 1:
      divisor = 256.0f;
      index_count = 2048;
      ImGui::TextUnformatted("0.0 to 8.0");
      break;
   case 2:
      divisor = 256.0f;
      index_count = 4096;
      ImGui::TextUnformatted("0.0 to 16.0");
      break;
   case 3:
      divisor = 128.0f;
      index_count = 4096;
      ImGui::TextUnformatted("0.0 to 32.0");
      break;
   case 4:
      divisor = 64.0f;
      index_count = 4096;
      ImGui::TextUnformatted("0.0 to 64.0");
      break;
   }
}

void show_fog_imgui(Postprocess_fog& fog) noexcept
{
   auto& params = fog.fog_params();
   bool enabled = fog.enabled();

   if (ImGui::CollapsingHeader("Basic Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::Checkbox("Enabled", &enabled)) {
         fog.enabled(enabled);
      }

      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Enable SWBF3-style post-process fog.\n\n"
            "This applies distance and height-based fog to the scene "
            "after geometry rendering.");
      }
   }

   ImGui::BeginDisabled(!enabled);

   if (ImGui::CollapsingHeader("Distance Fog", ImGuiTreeNodeFlags_DefaultOpen)) {
      // SWBF3: fog[0-3] = RGBA where A is intensity
      ImGui::ColorEdit4("Fog Color", &params.fog_color.x,
                        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "SWBF3: fog[0-3]\n"
            "RGB = fog color\n"
            "Alpha = fog intensity (0 = no distance fog)");
      }

      ImGui::DragFloat("Start Distance", &params.fog_start, 1.0f, 0.0f, 10000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Distance at which fog begins (SWBF3: fogNear)");
      }

      ImGui::DragFloat("End Distance", &params.fog_end, 1.0f, 0.0f, 10000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Distance at which fog reaches maximum (SWBF3: fogFar)");
      }

      params.fog_color.w = std::clamp(params.fog_color.w, 0.0f, 2.0f);
      params.fog_start = std::clamp(params.fog_start, 0.0f, 10000.0f);
      params.fog_end = std::clamp(params.fog_end, params.fog_start, 10000.0f);
   }

   if (ImGui::CollapsingHeader("Height/Atmospheric Fog", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::DragFloat("Height Base", &params.height_base, 1.0f, -1000.0f, 1000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Ground level for height fog (SWBF3: fogMinHeight)");
      }

      ImGui::DragFloat("Height Ceiling", &params.height_ceiling, 1.0f, -1000.0f, 1000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Top of the fog layer (SWBF3: fogMaxHeight)\n\n"
            "Objects above this still receive fog based on how much of the\n"
            "view ray passes through the layer (ray-through-layer model).");
      }

      ImGui::DragFloat("Atmosphere Density", &params.atmos_density, 0.001f, 0.0f, 0.1f, "%.4f");
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Density multiplier for atmospheric fog (SWBF3: fogDensity)\n\n"
            "Uses sqrt(distance) falloff for characteristic SWBF3 look.");
      }

      ImGui::DragFloat("Fog Alpha", &params.fog_alpha, 0.01f, 0.0f, 1.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Minimum atmosphere for objects above the fog layer (SWBF3: fogAlpha)\n\n"
            "Objects above fogMaxHeight still accumulate fog because the view ray\n"
            "passes through the fog layer. This value sets the residual haze\n"
            "for those above-layer objects. 0 = no residual, 1 = full haze.");
      }

      ImGui::DragFloat("Height Falloff", &params.height_falloff, 0.1f, 0.0f, 10.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Controls how quickly above-layer objects blend to Fog Alpha.\n\n"
            "Higher values = faster transition to residual haze.\n"
            "1.0 = standard blend (SWBF3 default).");
      }

      ImGui::DragFloat("Ceiling Fade", &params.ceiling_fade, 1.0f, 0.0f, 200.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Distance above the ceiling where fog smoothly fades to zero.\n\n"
            "Compensates for per-pixel precision - SWBF3's per-vertex fog\n"
            "naturally smoothed across polygon boundaries.\n"
            "0 = hard cutoff at ceiling (mathematically accurate to SWBF3).");
      }

      bool use_distance_range = params.height_fog_use_distance_range != 0;
      if (ImGui::Checkbox("Use Distance Range", &use_distance_range)) {
         params.height_fog_use_distance_range = use_distance_range ? 1 : 0;
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "When enabled, height fog respects Start/End Distance.\n\n"
            "Creates a clear 'bubble' around the camera where height fog\n"
            "doesn't appear, then fades in from Start to End Distance.");
      }

      ImGui::DragFloat("Immersion", &params.fog_immersion, 0.01f, 0.0f, 1.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Near-field fog when camera is in the fog layer.\n\n"
            "0 = Off (fog only affects distant objects)\n"
            "1 = Full (nearby objects also get fogged when you're in the fog)\n\n"
            "Makes fog feel thicker/more immersive when standing inside it.");
      }

      ImGui::DragFloat("Immersion Start", &params.immersion_start, 1.0f, 0.0f, 1000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Distance where immersion fog starts fading in.\n\n"
            "Objects closer than this have no immersion fog (clear at feet).");
      }

      ImGui::DragFloat("Immersion End", &params.immersion_end, 1.0f, 0.0f, 1000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Distance where immersion fog reaches full strength.\n\n"
            "Fog fades in from Start to End, then fades out toward Range.");
      }

      ImGui::DragFloat("Immersion Range", &params.immersion_range, 1.0f, 10.0f, 1000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Distance where immersion fog fades out to zero.\n\n"
            "Beyond this distance, no immersion fog is applied.");
      }

      params.height_ceiling = std::max(params.height_ceiling, params.height_base + 1.0f);
      params.atmos_density = std::clamp(params.atmos_density, 0.0f, 0.1f);
      params.fog_alpha = std::clamp(params.fog_alpha, 0.0f, 1.0f);
      params.height_falloff = std::clamp(params.height_falloff, 0.0f, 10.0f);
      params.ceiling_fade = std::clamp(params.ceiling_fade, 0.0f, 200.0f);
      params.fog_immersion = std::clamp(params.fog_immersion, 0.0f, 1.0f);
      params.immersion_start = std::clamp(params.immersion_start, 0.0f, params.immersion_end);
      params.immersion_end = std::clamp(params.immersion_end, params.immersion_start, params.immersion_range);
      params.immersion_range = std::clamp(params.immersion_range, params.immersion_end, 1000.0f);
   }

   if (ImGui::CollapsingHeader("Disc Boundary")) {
      ImGui::DragFloat("Disc Radius", &params.fog_disc_radius, 10.0f, 0.0f, 100000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Radius of the fog disc in world units.\n\n"
            "0 = disabled (fog extends infinitely in XZ).\n"
            "When set, fog is limited to a circular area, giving\n"
            "the horizon a curved appearance instead of a flat line.");
      }

      ImGui::DragFloat("Disc Center X", &params.fog_disc_center_x, 1.0f, -100000.0f, 100000.0f);
      ImGui::DragFloat("Disc Center Z", &params.fog_disc_center_z, 1.0f, -100000.0f, 100000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("World-space XZ center of the fog disc.");
      }

      ImGui::DragFloat("Disc Edge Fade", &params.fog_disc_edge_fade, 1.0f, 0.0f, 1000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Distance over which fog fades at the disc edge.\n\n"
            "Larger values = softer edge transition.");
      }

      params.fog_disc_radius = std::max(params.fog_disc_radius, 0.0f);
      params.fog_disc_edge_fade = std::clamp(params.fog_disc_edge_fade, 0.0f, 1000.0f);
   }

   if (ImGui::CollapsingHeader("Options")) {
      bool blend_additive = params.blend_additive != 0;
      if (ImGui::Checkbox("Additive Blend", &blend_additive)) {
         params.blend_additive = blend_additive ? 1 : 0;
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Use additive blending instead of alpha blending (SWBF3: fogAdd)\n\n"
            "Additive: color + fog_color * factor\n"
            "Alpha: lerp(color, fog_color, factor)");
      }

      bool apply_to_sky = params.apply_to_sky != 0;
      if (ImGui::Checkbox("Apply to Sky", &apply_to_sky)) {
         params.apply_to_sky = apply_to_sky ? 1 : 0;
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Whether to apply fog to the sky (SWBF3: fogSky)");
      }
   }

   if (ImGui::CollapsingHeader("Presets")) {
      if (ImGui::Button("Light Haze")) {
         params.fog_color = {0.85f, 0.9f, 0.95f, 0.3f};  // RGBA with intensity
         params.fog_start = 50.0f;
         params.fog_end = 500.0f;
         params.height_base = 0.0f;
         params.height_ceiling = 200.0f;
         params.atmos_density = 0.005f;
         params.fog_alpha = 0.1f;
         params.height_falloff = 1.0f;
         params.ceiling_fade = 0.0f;
         params.fog_immersion = 0.0f;
         params.immersion_start = 0.0f;
         params.immersion_end = 20.0f;
         params.immersion_range = 240.0f;
         params.height_fog_use_distance_range = 0;
         params.blend_additive = 0;
         params.fog_disc_radius = 0.0f;
      }
      ImGui::SameLine();

      if (ImGui::Button("Dense Fog")) {
         params.fog_color = {0.7f, 0.75f, 0.8f, 0.8f};
         params.fog_start = 10.0f;
         params.fog_end = 150.0f;
         params.height_base = 0.0f;
         params.height_ceiling = 100.0f;
         params.atmos_density = 0.02f;
         params.fog_alpha = 0.3f;
         params.height_falloff = 1.0f;
         params.ceiling_fade = 0.0f;
         params.fog_immersion = 0.5f;  // More immersive when in dense fog
         params.immersion_start = 0.0f;
         params.immersion_end = 20.0f;
         params.immersion_range = 240.0f;
         params.height_fog_use_distance_range = 0;
         params.blend_additive = 0;
         params.fog_disc_radius = 0.0f;
      }
      ImGui::SameLine();

      if (ImGui::Button("Sunset Atmosphere")) {
         params.fog_color = {1.0f, 0.7f, 0.5f, 0.5f};
         params.fog_start = 100.0f;
         params.fog_end = 800.0f;
         params.height_base = 0.0f;
         params.height_ceiling = 300.0f;
         params.atmos_density = 0.008f;
         params.fog_alpha = 0.15f;
         params.height_falloff = 1.0f;
         params.ceiling_fade = 0.0f;
         params.fog_immersion = 0.0f;
         params.immersion_start = 0.0f;
         params.immersion_end = 20.0f;
         params.immersion_range = 240.0f;
         params.height_fog_use_distance_range = 0;
         params.blend_additive = 0;
         params.fog_disc_radius = 0.0f;
      }

      if (ImGui::Button("SWBF3 Default")) {
         params.fog_color = {1.0f, 1.0f, 1.0f, 0.0f};  // Default: fog disabled
         params.fog_start = 0.0f;
         params.fog_end = 80.0f;
         params.height_base = 0.0f;
         params.height_ceiling = 250.0f;
         params.atmos_density = 0.012f;
         params.fog_alpha = 0.0f;
         params.height_falloff = 1.0f;
         params.ceiling_fade = 0.0f;
         params.fog_immersion = 0.0f;
         params.immersion_start = 0.0f;
         params.immersion_end = 20.0f;
         params.immersion_range = 240.0f;
         params.height_fog_use_distance_range = 0;
         params.blend_additive = 0;
         params.fog_disc_radius = 0.0f;
         params.apply_to_sky = 1;  // fogSky default is true in SWBF3
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Reset to SWBF3 SceneVolumeData default values");
      }
   }

   if (ImGui::CollapsingHeader("Debug")) {
      const char* debug_modes[] = {"None", "Depth", "World Y"};
      int current_mode = static_cast<int>(fog.debug_mode());
      if (ImGui::Combo("Debug Mode", &current_mode, debug_modes, 3)) {
         fog.debug_mode(static_cast<Fog_debug_mode>(current_mode));
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "None: Normal fog rendering\n"
            "Depth: R=raw depth, G=linear depth (normalized), B=params valid\n"
            "World Y: R=world Y position, G=height fog factor, B=in height range");
      }
   }

   ImGui::EndDisabled();
}

// Random cloud preset generators
namespace {
   thread_local std::mt19937 cloud_rng{std::random_device{}()};

   float rand_range(float min, float max) {
      std::uniform_real_distribution<float> dist(min, max);
      return dist(cloud_rng);
   }

   float vary(float base, float variance) {
      return base + rand_range(-variance, variance);
   }

   glm::vec3 vary_color(glm::vec3 base, float variance) {
      return glm::clamp(glm::vec3{
         vary(base.x, variance),
         vary(base.y, variance),
         vary(base.z, variance)
      }, 0.0f, 1.0f);
   }
}

core::cb::CloudLayerParams generate_random_stratus() noexcept
{
   auto p = core::cb::CloudLayerParams::stratus();

   // Randomize within stratus-appropriate ranges
   p.light_color = vary_color({0.85f, 0.85f, 0.88f}, 0.08f);
   p.dark_color = vary_color({0.4f, 0.42f, 0.45f}, 0.1f);
   p.cover = vary(0.65f, 0.15f);
   p.sharpness = vary(0.4f, 0.15f);
   p.altitude = vary(600.0f, 200.0f);
   p.plane_size = vary(8.0f, 2.0f);
   p.tiling_scale = vary(0.8f, 0.3f);
   p.wind_speed = vary(0.001f, 0.0005f);
   p.wind_angle = rand_range(0.0f, 360.0f);
   p.scattering = vary(0.08f, 0.03f);

   // Vary octave weights slightly
   p.octave_weights_0to3.x = vary(1.0f, 0.2f);
   p.octave_weights_0to3.y = vary(0.3f, 0.1f);

   p.cover = std::clamp(p.cover, 0.3f, 0.85f);
   p.sharpness = std::clamp(p.sharpness, 0.2f, 0.6f);
   p.altitude = std::clamp(p.altitude, 300.0f, 900.0f);

   return p;
}

core::cb::CloudLayerParams generate_random_cumulus() noexcept
{
   auto p = core::cb::CloudLayerParams::cumulus();

   // Randomize within cumulus-appropriate ranges
   p.light_color = vary_color({1.0f, 0.95f, 0.88f}, 0.06f);
   p.dark_color = vary_color({0.25f, 0.28f, 0.35f}, 0.1f);
   p.cover = vary(0.45f, 0.2f);
   p.sharpness = vary(1.2f, 0.4f);
   p.altitude = vary(1400.0f, 400.0f);
   p.plane_size = vary(6.0f, 1.5f);
   p.tiling_scale = vary(1.5f, 0.5f);
   p.wind_speed = vary(0.002f, 0.001f);
   p.wind_angle = rand_range(0.0f, 360.0f);
   p.scattering = vary(0.06f, 0.02f);

   // Vary octave weights
   p.octave_weights_0to3.x = vary(0.9f, 0.2f);
   p.octave_weights_0to3.y = vary(0.5f, 0.15f);
   p.octave_weights_0to3.z = vary(0.3f, 0.1f);

   p.cover = std::clamp(p.cover, 0.2f, 0.7f);
   p.sharpness = std::clamp(p.sharpness, 0.7f, 1.8f);
   p.altitude = std::clamp(p.altitude, 900.0f, 2200.0f);

   return p;
}

core::cb::CloudLayerParams generate_random_cirrus() noexcept
{
   auto p = core::cb::CloudLayerParams::cirrus();

   // Randomize within cirrus-appropriate ranges
   p.light_color = vary_color({1.0f, 0.98f, 1.0f}, 0.04f);
   p.dark_color = vary_color({0.6f, 0.65f, 0.75f}, 0.1f);
   p.cover = vary(0.3f, 0.15f);
   p.sharpness = vary(0.25f, 0.1f);
   p.altitude = vary(3500.0f, 800.0f);
   p.plane_size = vary(10.0f, 3.0f);
   p.tiling_scale = vary(2.5f, 0.8f);
   p.wind_speed = vary(0.004f, 0.002f);
   p.wind_angle = rand_range(0.0f, 360.0f);
   p.scattering = vary(0.02f, 0.01f);

   // Cirrus needs more high-frequency detail
   p.octave_weights_0to3.x = vary(0.6f, 0.15f);
   p.octave_weights_0to3.z = vary(0.35f, 0.1f);
   p.octave_weights_4to7.x = vary(0.15f, 0.05f);

   p.cover = std::clamp(p.cover, 0.1f, 0.5f);
   p.sharpness = std::clamp(p.sharpness, 0.1f, 0.4f);
   p.altitude = std::clamp(p.altitude, 2500.0f, 5000.0f);
   p.scattering = std::clamp(p.scattering, 0.005f, 0.04f);

   return p;
}

// Helper to show per-layer controls
void show_cloud_layer_params_imgui(core::cb::CloudLayerParams& layer, int layer_idx) noexcept
{
   ImGui::PushID(layer_idx);

   bool layer_enabled = layer.enabled != 0;
   if (ImGui::Checkbox("Enabled", &layer_enabled)) {
      layer.enabled = layer_enabled ? 1 : 0;
   }

   ImGui::BeginDisabled(!layer_enabled);

   if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::DragFloat("Altitude", &layer.altitude, 10.0f, 0.0f, 50000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Height of the cloud plane (SWBF3: cloudLayerPlaneAltitude)");
      }

      ImGui::DragFloat("Curved Radius", &layer.curved_radius, 50.0f, 0.0f, 100000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Curvature radius for the cloud plane (SWBF3: cloudLayerCurvedPlaneRadius)\n\n"
            "Smaller values = more curvature. 0 = flat plane.");
      }

      ImGui::DragFloat("Plane Size", &layer.plane_size, 0.1f, 0.1f, 50.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Plane size multiplier (SWBF3: cloudLayerPlaneSizeScale)\n\n"
            "Default plane is 2000 units wide (1000 each side).\n"
            "5.0 = 10000 units total.");
      }

      ImGui::DragFloat("Center X", &layer.plane_center_x, 10.0f, -100000.0f, 100000.0f);
      ImGui::DragFloat("Center Z", &layer.plane_center_z, 10.0f, -100000.0f, 100000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "World-space center of the cloud plane.\n\n"
            "Clouds are fixed in world space, not camera-relative.");
      }

      layer.altitude = std::max(layer.altitude, 0.0f);
      layer.curved_radius = std::max(layer.curved_radius, 0.0f);
      layer.plane_size = std::clamp(layer.plane_size, 0.1f, 50.0f);
   }

   if (ImGui::CollapsingHeader("Cloud Shape")) {
      ImGui::DragFloat("Cover", &layer.cover, 0.01f, 0.0f, 1.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Cloud coverage (SWBF3: cloudLayerCover)\n\n"
            "0 = no clouds, 1 = fully covered.\n"
            "Controls how much of the noise becomes visible cloud.");
      }

      ImGui::DragFloat("Sharpness", &layer.sharpness, 0.01f, 0.01f, 2.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Edge softness (SWBF3: cloudLayerSharpness)\n\n"
            "Lower values = sharper cloud edges.\n"
            "Higher values = softer, more diffuse edges.");
      }

      ImGui::DragFloat("Tiling Scale", &layer.tiling_scale, 0.1f, 0.1f, 20.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Noise texture tiling (SWBF3: cloudLayerNoiseTexTilingScale)");
      }

      ImGui::DragFloat("Half Height", &layer.half_height, 10.0f, 1.0f, 5000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Vertical scale of cloud noise (SWBF3: cloudLayerHalfHeight)");
      }

      layer.cover = std::clamp(layer.cover, 0.0f, 1.0f);
      layer.sharpness = std::clamp(layer.sharpness, 0.01f, 2.0f);
      layer.tiling_scale = std::clamp(layer.tiling_scale, 0.1f, 20.0f);
      layer.half_height = std::clamp(layer.half_height, 1.0f, 5000.0f);
   }

   if (ImGui::CollapsingHeader("Lighting")) {
      ImGui::ColorEdit3("Light Color", &layer.light_color.x, ImGuiColorEditFlags_Float);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Sunlit cloud color (SWBF3: cloudLayerLightColor)");
      }

      ImGui::ColorEdit3("Dark Color", &layer.dark_color.x, ImGuiColorEditFlags_Float);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Shadow/underside cloud color (SWBF3: cloudLayerDarkColor)");
      }

      ImGui::DragFloat("Scattering", &layer.scattering, 0.01f, 0.0f, 2.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Light scattering coefficient (SWBF3: cloudLayerLightScattering)\n\n"
            "Controls how much light is absorbed by cloud mass.\n"
            "Higher = darker shadows.");
      }

      ImGui::DragFloat("Lightray Step", &layer.lightray_step, 0.01f, 0.01f, 5.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Step length for light marching (SWBF3: cloudLayerLightrayStepLength)\n\n"
            "Controls how far light rays are traced through clouds.\n"
            "Larger = softer shadows from distant cloud mass.");
      }

      ImGui::DragFloat("Max Lighting", &layer.max_lighting, 0.01f, 0.0f, 2.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Maximum brightness clamp (SWBF3: cloudLayerMaxLighting)");
      }

      ImGui::DragFloat("Min Lighting", &layer.min_lighting, 0.01f, 0.0f, 1.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Minimum brightness floor (SWBF3: cloudLayerMinLighting)");
      }

      layer.scattering = std::clamp(layer.scattering, 0.0f, 2.0f);
      layer.lightray_step = std::clamp(layer.lightray_step, 0.01f, 5.0f);
      layer.max_lighting = std::clamp(layer.max_lighting, layer.min_lighting, 2.0f);
      layer.min_lighting = std::clamp(layer.min_lighting, 0.0f, layer.max_lighting);
   }

   if (ImGui::CollapsingHeader("Wind")) {
      ImGui::DragFloat("Wind Speed", &layer.wind_speed, 0.0001f, 0.0f, 0.1f, "%.5f");
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Cloud scroll speed (SWBF3: cloudLayerWindSpeed)");
      }

      ImGui::DragFloat("Wind Angle", &layer.wind_angle, 1.0f, 0.0f, 360.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Wind direction in degrees from X axis (SWBF3: cloudLayerWindAngleFromXAxis)");
      }

      layer.wind_speed = std::clamp(layer.wind_speed, 0.0f, 0.1f);
      layer.wind_angle = std::fmod(std::max(layer.wind_angle, 0.0f), 360.0f);
   }

   if (ImGui::CollapsingHeader("Noise Octaves")) {
      ImGui::DragFloat4("Weights 0-3", &layer.octave_weights_0to3.x, 0.01f, 0.0f, 2.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Octave weights for channels 0-3 (SWBF3: cloudLayerNoiseOctaveWeights)");
      }

      ImGui::DragFloat4("Weights 4-7", &layer.octave_weights_4to7.x, 0.01f, 0.0f, 2.0f);

      ImGui::DragFloat4("Evol Freqs 0-3", &layer.octave_evol_freqs_0to3.x, 0.001f, 0.0f, 5.0f, "%.4f");
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Evolution frequencies (SWBF3: cloudLayerNoiseOctaveEvolFreqs)\n\n"
                           "Controls animation speed per octave.");
      }

      ImGui::DragFloat4("Evol Freqs 4-7", &layer.octave_evol_freqs_4to7.x, 0.01f, 0.0f, 5.0f, "%.4f");
   }

   // Generate random cloud preset buttons
   ImGui::Separator();
   ImGui::Text("Generate Cloud Type:");

   if (ImGui::Button("Stratus")) {
      layer = generate_random_stratus();
      layer.enabled = 1;
   }
   if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Generate randomized Stratus clouds\n(Low, flat, uniform gray layers)\n\nClick multiple times for variations");
   }

   ImGui::SameLine();
   if (ImGui::Button("Cumulus")) {
      layer = generate_random_cumulus();
      layer.enabled = 1;
   }
   if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Generate randomized Cumulus clouds\n(Mid-level, puffy, well-defined)\n\nClick multiple times for variations");
   }

   ImGui::SameLine();
   if (ImGui::Button("Cirrus")) {
      layer = generate_random_cirrus();
      layer.enabled = 1;
   }
   if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Generate randomized Cirrus clouds\n(High altitude, wispy ice crystals)\n\nClick multiple times for variations");
   }

   ImGui::SameLine();
   if (ImGui::Button("Clear")) {
      layer = core::cb::CloudLayerParams{};
      layer.enabled = 0;
   }
   if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Clear this layer (disable and reset to defaults)");
   }

   ImGui::EndDisabled();
   ImGui::PopID();
}

void show_cloud_layer_imgui(Cloud_layer& cloud) noexcept
{
   auto& params = cloud.params();
   bool enabled = cloud.enabled();

   if (ImGui::CollapsingHeader("Basic Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::Checkbox("Enable Cloud System", &enabled)) {
         cloud.enabled(enabled);
      }

      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Enable SWBF3-style 3-layer cloud system.\n\n"
            "Renders procedural clouds on curved planes using\n"
            "noise octave blending and lightray-stepped lighting.\n\n"
            "Each layer can be independently enabled/disabled.");
      }

      ImGui::DragFloat3("Sun Direction", &params.sun_direction.x, 0.01f, -1.0f, 1.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Direction toward the sun (shared by all layers)");
      }
   }

   ImGui::BeginDisabled(!enabled);

   // Show tabs for each layer (Stratus, Cumulus, Cirrus)
   static const char* layer_names[] = {"Stratus", "Cumulus", "Cirrus"};
   static const char* layer_tooltips[] = {
      "Low altitude, flat, uniform gray layer clouds",
      "Mid altitude, puffy, well-defined clouds",
      "High altitude, wispy ice crystal clouds"
   };

   if (ImGui::BeginTabBar("CloudLayerTabs")) {
      for (int i = 0; i < 3; i++) {
         char tab_name[32];
         snprintf(tab_name, sizeof(tab_name), "%s%s", layer_names[i],
                  params.layers[i].enabled ? "" : " (off)");

         if (ImGui::BeginTabItem(tab_name)) {
            ImGui::TextDisabled("(%s)", layer_tooltips[i]);
            ImGui::Separator();
            show_cloud_layer_params_imgui(params.layers[i], i);
            ImGui::EndTabItem();
         }
      }
      ImGui::EndTabBar();
   }

   ImGui::Separator();

   if (ImGui::CollapsingHeader("Quick Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
      // Sky type presets
      if (ImGui::Button("Randomize All")) {
         params.layers[0] = generate_random_stratus();
         params.layers[0].enabled = 1;
         params.layers[1] = generate_random_cumulus();
         params.layers[1].enabled = 1;
         params.layers[2] = generate_random_cirrus();
         params.layers[2].enabled = 1;
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Generate randomized Stratus + Cumulus + Cirrus\n\nClick multiple times for different skies!");
      }

      ImGui::SameLine();
      if (ImGui::Button("Clear Sky")) {
         for (auto& layer : params.layers) {
            layer.enabled = 0;
         }
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Disable all cloud layers");
      }

      ImGui::SameLine();
      if (ImGui::Button("Reset Defaults")) {
         params = core::cb::CloudLayers{};
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Reset all layers to default Stratus/Cumulus/Cirrus");
      }

      // Weather presets
      if (ImGui::Button("Fair Weather")) {
         params.layers[0].enabled = 0;
         params.layers[1] = generate_random_cumulus();
         params.layers[1].cover = rand_range(0.25f, 0.4f);
         params.layers[1].enabled = 1;
         params.layers[2] = generate_random_cirrus();
         params.layers[2].cover = rand_range(0.15f, 0.25f);
         params.layers[2].enabled = 1;
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Scattered cumulus with light cirrus\n(Typical nice day)");
      }

      ImGui::SameLine();
      if (ImGui::Button("Overcast")) {
         params.layers[0] = generate_random_stratus();
         params.layers[0].cover = rand_range(0.7f, 0.85f);
         params.layers[0].enabled = 1;
         params.layers[1] = generate_random_stratus();
         params.layers[1].altitude = rand_range(900.0f, 1200.0f);
         params.layers[1].cover = rand_range(0.5f, 0.7f);
         params.layers[1].enabled = 1;
         params.layers[2].enabled = 0;
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Dense low stratus layers\n(Gray, overcast sky)");
      }

      ImGui::SameLine();
      if (ImGui::Button("Stormy")) {
         params.layers[0] = generate_random_stratus();
         params.layers[0].cover = rand_range(0.75f, 0.9f);
         params.layers[0].dark_color = glm::vec3{0.2f, 0.22f, 0.28f};
         params.layers[0].light_color = glm::vec3{0.5f, 0.52f, 0.55f};
         params.layers[0].enabled = 1;
         params.layers[1] = generate_random_cumulus();
         params.layers[1].cover = rand_range(0.6f, 0.8f);
         params.layers[1].dark_color = glm::vec3{0.15f, 0.18f, 0.25f};
         params.layers[1].altitude = rand_range(800.0f, 1100.0f);
         params.layers[1].enabled = 1;
         params.layers[2].enabled = 0;
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Dark, heavy cloud cover\n(Pre-storm conditions)");
      }

      if (ImGui::Button("High Altitude")) {
         params.layers[0].enabled = 0;
         params.layers[1] = generate_random_cirrus();
         params.layers[1].altitude = rand_range(2500.0f, 3500.0f);
         params.layers[1].enabled = 1;
         params.layers[2] = generate_random_cirrus();
         params.layers[2].altitude = rand_range(4000.0f, 5000.0f);
         params.layers[2].enabled = 1;
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Only high-altitude cirrus\n(Clear below, wispy above)");
      }

      ImGui::SameLine();
      if (ImGui::Button("Enable All")) {
         for (auto& layer : params.layers) {
            layer.enabled = 1;
         }
      }
      ImGui::SameLine();
      if (ImGui::Button("Disable All")) {
         for (auto& layer : params.layers) {
            layer.enabled = 0;
         }
      }
   }

   ImGui::EndDisabled();
}

void show_cloud_volume_imgui(Cloud_volume& cloud) noexcept
{
   auto& params = cloud.params();
   bool enabled = cloud.enabled();
   bool needs_regen = false;

   if (ImGui::CollapsingHeader("Basic Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::Checkbox("Enable Cloud Volumes", &enabled)) {
         cloud.enabled(enabled);
      }

      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip(
            "Enable SWBF3-style volumetric cloud volumes.\n\n"
            "Renders billboard cloud impostors scattered within\n"
            "a bounding region with 3D noise-based shaping.");
      }

      ImGui::DragFloat3("Sun Direction", &params.sun_direction.x, 0.01f, -1.0f, 1.0f);
   }

   ImGui::BeginDisabled(!enabled);

   if (ImGui::CollapsingHeader("Cloud Area", ImGuiTreeNodeFlags_DefaultOpen)) {
      needs_regen |= ImGui::DragFloat3("Area Min", &params.area_min.x, 10.0f, -100000.0f, 100000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Minimum bounds of the cloud volume area (world space)");
      }

      needs_regen |= ImGui::DragFloat3("Area Max", &params.area_max.x, 10.0f, -100000.0f, 100000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Maximum bounds of the cloud volume area (world space)");
      }

      int cloud_count = static_cast<int>(params.cloud_count);
      if (ImGui::SliderInt("Cloud Count", &cloud_count, 0, 128)) {
         params.cloud_count = static_cast<std::uint32_t>(cloud_count);
         needs_regen = true;
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Number of cloud volumes to generate");
      }

      int seed = static_cast<int>(params.seed);
      if (ImGui::InputInt("Seed", &seed)) {
         params.seed = static_cast<std::uint32_t>(seed);
         needs_regen = true;
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Random seed for cloud placement\nChange to get different arrangements");
      }
   }

   if (ImGui::CollapsingHeader("Cloud Size")) {
      needs_regen |= ImGui::DragFloat3("Size Min", &params.cloud_size_min.x, 10.0f, 10.0f, 10000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Minimum cloud dimensions (width, height, depth)");
      }

      needs_regen |= ImGui::DragFloat3("Size Max", &params.cloud_size_max.x, 10.0f, 10.0f, 10000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Maximum cloud dimensions (width, height, depth)");
      }
   }

   if (ImGui::CollapsingHeader("Appearance")) {
      ImGui::DragFloat("Sharpness", &params.sharpness, 0.01f, 0.0f, 1.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Edge sharpness of cloud volumes");
      }

      ImGui::DragFloat("Density", &params.density, 0.01f, 0.0f, 2.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Overall cloud density/opacity");
      }

      ImGui::DragFloat("Noise Influence", &params.noise_influence, 0.01f, 0.0f, 1.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("How much 3D noise affects cloud shape\n0 = smooth spheres, 1 = full noise");
      }

      ImGui::DragFloat("Noise Tiling", &params.noise_tiling, 0.1f, 0.1f, 10.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Scale of the 3D noise pattern");
      }

      ImGui::DragFloat("Edge Softness", &params.edge_softness, 0.01f, 0.0f, 1.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Softness of cloud edges");
      }
   }

   if (ImGui::CollapsingHeader("Lighting")) {
      ImGui::ColorEdit3("Light Color", &params.light_color.x, ImGuiColorEditFlags_Float);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Sunlit cloud color");
      }

      ImGui::ColorEdit3("Dark Color", &params.dark_color.x, ImGuiColorEditFlags_Float);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Shadow/underside cloud color");
      }

      ImGui::DragFloat("Light Scattering", &params.light_scattering, 0.1f, 0.0f, 10.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Light scattering/absorption coefficient");
      }

      ImGui::DragFloat("Max Lighting", &params.max_lighting, 0.01f, 0.0f, 2.0f);
      ImGui::DragFloat("Min Lighting", &params.min_lighting, 0.01f, 0.0f, 1.0f);
   }

   if (ImGui::CollapsingHeader("Depth & Distance")) {
      ImGui::DragFloat("Depth Fade Near", &params.depth_fade_near, 1.0f, 1.0f, 1000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Distance over which clouds fade when intersecting geometry");
      }

      ImGui::DragFloat("Depth Fade Far", &params.depth_fade_far, 10.0f, 10.0f, 10000.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Maximum draw distance for cloud volumes");
      }
   }

   if (ImGui::CollapsingHeader("Animation")) {
      ImGui::DragFloat("Wind Speed", &params.wind_speed, 0.001f, 0.0f, 0.1f, "%.4f");
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Cloud drift speed");
      }

      ImGui::DragFloat("Wind Angle", &params.wind_angle, 1.0f, 0.0f, 360.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Wind direction in degrees");
      }

      ImGui::DragFloat("Evolution Speed", &params.evolution_speed, 0.01f, 0.0f, 1.0f);
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("How fast cloud shapes evolve over time");
      }
   }

   if (ImGui::CollapsingHeader("Presets")) {
      if (ImGui::Button("Regenerate")) {
         needs_regen = true;
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Regenerate cloud positions with current settings");
      }

      ImGui::SameLine();
      if (ImGui::Button("Sparse Cumulus")) {
         params.area_min = {-8000.0f, 1000.0f, -8000.0f};
         params.area_max = {8000.0f, 2500.0f, 8000.0f};
         params.cloud_size_min = {600.0f, 300.0f, 600.0f};
         params.cloud_size_max = {1500.0f, 600.0f, 1500.0f};
         params.cloud_count = 40;
         params.sharpness = 0.8f;
         params.density = 1.0f;
         params.noise_influence = 0.6f;
         needs_regen = true;
      }

      ImGui::SameLine();
      if (ImGui::Button("Dense Field")) {
         params.area_min = {-5000.0f, 800.0f, -5000.0f};
         params.area_max = {5000.0f, 2000.0f, 5000.0f};
         params.cloud_size_min = {800.0f, 400.0f, 800.0f};
         params.cloud_size_max = {2000.0f, 800.0f, 2000.0f};
         params.cloud_count = 80;
         params.sharpness = 0.7f;
         params.density = 1.2f;
         params.noise_influence = 0.7f;
         needs_regen = true;
      }

      if (ImGui::Button("High Altitude")) {
         params.area_min = {-10000.0f, 2500.0f, -10000.0f};
         params.area_max = {10000.0f, 4000.0f, 10000.0f};
         params.cloud_size_min = {1000.0f, 200.0f, 1000.0f};
         params.cloud_size_max = {3000.0f, 400.0f, 3000.0f};
         params.cloud_count = 50;
         params.sharpness = 0.5f;
         params.density = 0.8f;
         params.noise_influence = 0.5f;
         needs_regen = true;
      }

      ImGui::SameLine();
      if (ImGui::Button("Reset")) {
         params = core::cb::CloudVolumes{};
         needs_regen = true;
      }
   }

   if (needs_regen) {
      cloud.regenerate_clouds();
   }

   ImGui::EndDisabled();
}
}
}

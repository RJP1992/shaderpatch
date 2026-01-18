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
#include <sstream>
#include <string_view>

#include <fmt/format.h>

#include "../imgui/imgui.h"

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

        Fog_params show_fog_imgui(Fog_params params) noexcept;

        Cloud_params show_clouds_imgui(Cloud_params params) noexcept;

        SSAO_params show_ssao_imgui(SSAO_params params) noexcept;

        FFX_cas_params show_ffx_cas_imgui(FFX_cas_params params) noexcept;

        void show_tonemapping_curve(std::function<float(float)> tonemapper) noexcept;
    }

    Control::Control(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept
        : postprocess{ device, shaders },
        cmaa2{ device, shaders.compute("CMAA2"sv) },
        ssao{ device, shaders },
        ffx_cas{ device, shaders },
        mask_nan{ device, shaders },
        clouds{ device, shaders },
        cubemap_debug{ device, shaders },
        debug_stencil{ device, shaders },
        sky_dome{ device, shaders },
        skybox_override{ device, shaders },
        profiler{ device }
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
        ImGui::SetNextWindowSize({ 533, 591 }, ImGuiCond_FirstUseEver);
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
        postprocess.fog_params(config["Fog"s].as<Fog_params>(Fog_params{}));
        ssao.params(config["SSAO"s].as<SSAO_params>(SSAO_params{ false }));
        ffx_cas.params(config["ContrastAdaptiveSharpening"s].as<FFX_cas_params>(
            FFX_cas_params{ false }));
        clouds.params(config["Clouds"s].as<Cloud_params>(Cloud_params{}));
        sky_dome.params(config["SkyDome"s].as<Sky_dome_params>(Sky_dome_params{}));
        _cubemap_alignment = config["CubemapAlignment"s].as<Cubemap_alignment>(Cubemap_alignment{});
        skybox_override.params(config["SkyboxOverride"s].as<Skybox_override_params>(Skybox_override_params{}));

        // Backwards compat: migrate cubemap alignment from Fog if present
        if (config["Fog"s]["CubemapRotation"s]) {
            _cubemap_alignment.rotation = config["Fog"s]["CubemapRotation"s].as<glm::vec3>(_cubemap_alignment.rotation);
            _cubemap_alignment.scale = config["Fog"s]["CubemapScale"s].as<glm::vec3>(_cubemap_alignment.scale);
            _cubemap_alignment.offset = config["Fog"s]["CubemapOffset"s].as<glm::vec3>(_cubemap_alignment.offset);
            _cubemap_alignment.mip_scale = config["Fog"s]["CubemapMipScale"s].as<float>(_cubemap_alignment.mip_scale);
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
        config["Fog"s] = postprocess.fog_params();
        config["SSAO"s] = ssao.params();
        config["ContrastAdaptiveSharpening"s] = ffx_cas.params();
        config["Clouds"s] = clouds.params();
        config["SkyDome"s] = sky_dome.params();
        config["CubemapAlignment"s] = _cubemap_alignment;
        config["SkyboxOverride"s] = skybox_override.params();

        std::stringstream stream;

        stream << "# Auto-Generated Effects Config. May have less than ideal formatting.\n"sv;

        stream << config;

        return stream.str();
    }

    void Control::save_params_to_yaml_file(const fs::path& save_to) noexcept
    {
        auto config = output_params_to_yaml_string();

        std::ofstream file{ save_to };

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
                std::span{ reinterpret_cast<std::byte*>(config.data()),
                          config.size() });
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
            if (auto path = win32::open_file_dialog({ {L"Effects Config", L"*.spfx"} },
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
            ImGui::TextColored({ 1.0f, 0.2f, 0.33f, 1.0f }, "Open Failed!");
        }

        ImGui::SameLine();

        if (ImGui::Button("Save Config")) {
            if (auto path = win32::save_file_dialog({ {L"Effects Config", L"*.spfx"} },
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
                win32::save_file_dialog({ {L"Munged Effects Config", L"*.mspfx"} }, game_window,
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
            ImGui::TextColored({ 1.0f, 0.2f, 0.33f, 1.0f }, "Save Failed!");
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

            if (ImGui::BeginTabItem("Fog")) {
                postprocess.fog_params(show_fog_imgui(postprocess.fog_params()));

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Environment")) {
                // Shared altitude thresholds (affects fog cubemap blending AND skybox_blend materials)
                if (ImGui::CollapsingHeader("Altitude Thresholds (BF3 Skyblend)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto fog = postprocess.fog_params();

                    ImGui::TextWrapped(
                        "Controls ground-to-space transitions. These values are automatically shared between "
                        "fog cubemap sampling and skybox_blend materials for synchronized blending.");

                    ImGui::DragFloat("Blend Start Altitude", &fog.altitude_blend_start, 10.0f, 0.0f, 10000.0f, "%.0f");
                    ImGui::SetItemTooltip("Camera altitude where space starts appearing.\nBelow this = 100%% ground/atmosphere.");

                    ImGui::DragFloat("Blend End Altitude", &fog.altitude_blend_end, 10.0f, 0.0f, 20000.0f, "%.0f");
                    ImGui::SetItemTooltip("Camera altitude where space is 100%%.\nAbove this = fully in space.");

                    // Ensure end > start
                    if (fog.altitude_blend_end < fog.altitude_blend_start) {
                        fog.altitude_blend_end = fog.altitude_blend_start + 100.0f;
                    }

                    postprocess.fog_params(fog);
                }

                // Cubemap textures for fog/atmosphere sampling
                if (ImGui::CollapsingHeader("Cubemaps", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto fog = postprocess.fog_params();

                    ImGui::TextWrapped("Cubemaps for atmosphere color sampling. Used by fog and environment effects.");

                    ImGui::InputText("Atmosphere Cubemap", fog.atmosphere_texture_name);
                    ImGui::SetItemTooltip("Ground-level atmosphere/sky cubemap.\nUsed for fog color sampling.");

                    ImGui::InputText("Space Cubemap", fog.space_texture_name);
                    ImGui::SetItemTooltip("Space/stars cubemap for high altitude.\nLeave empty to disable space blending.");

                    if (!fog.space_texture_name.empty()) {
                        ImGui::DragFloat("Manual Blend Override", &fog.sky_blend_override, 0.01f, 0.0f, 1.0f);
                        ImGui::SetItemTooltip("0 = auto blend from altitude.\n>0 = fixed blend value (0.5 = 50%% space).");
                    }

                    postprocess.fog_params(fog);
                }

                // Shared cubemap alignment (affects fog, sky, atmosphere)
                if (ImGui::CollapsingHeader("Cubemap Alignment")) {
                    auto fog = postprocess.fog_params();

                    ImGui::TextWrapped("Align cubemaps to match existing SWBF2 skyboxes.\nRequires an Atmosphere Cubemap to be set.");

                    ImGui::DragFloat3("Rotation (P/Y/R)", &fog.cubemap_rotation.x, 1.0f, -180.0f, 180.0f, "%.1f");
                    ImGui::SetItemTooltip("Euler angles in degrees (Pitch, Yaw, Roll).");

                    ImGui::DragFloat3("Scale", &fog.cubemap_scale.x, 0.01f, 0.1f, 10.0f, "%.2f");
                    ImGui::SetItemTooltip("Per-axis scale for cubemap lookup.");

                    ImGui::DragFloat3("Offset", &fog.cubemap_offset.x, 0.01f, -1.0f, 1.0f, "%.3f");
                    ImGui::SetItemTooltip("Direction offset applied before normalizing.");

                    if (ImGui::Button("Reset Transform")) {
                        fog.cubemap_rotation = {0.0f, 0.0f, 0.0f};
                        fog.cubemap_scale = {1.0f, 1.0f, 1.0f};
                        fog.cubemap_offset = {0.0f, 0.0f, 0.0f};
                    }

                    ImGui::Separator();
                    ImGui::DragFloat("Mip Blur Scale", &fog.cubemap_mip_scale, 0.05f, 0.0f, 2.0f, "%.2f");
                    ImGui::SetItemTooltip("Blur cubemap for close geometry to prevent\nbaked-in sun from showing through objects.\n0 = off, 0.5 = subtle, 1.0+ = strong blur.");

                    ImGui::Separator();
                    ImGui::Checkbox("Debug Visualizer", &fog.cubemap_debug_enabled);
                    ImGui::SetItemTooltip("Render a debug cube showing cubemap alignment.\nHelps align the transform to match existing skybox features.");

                    if (fog.cubemap_debug_enabled) {
                        ImGui::Checkbox("Render at Infinity", &fog.cubemap_debug_at_infinity);
                        ImGui::SetItemTooltip("ON = Cube renders like a skybox (always behind scene).\nOFF = Cube renders at a fixed world distance.");

                        if (!fog.cubemap_debug_at_infinity) {
                            ImGui::DragFloat("Debug Distance", &fog.cubemap_debug_distance, 100.0f, 100.0f, 50000.0f);
                            ImGui::SetItemTooltip("World distance for the debug cube when not at infinity.");
                        }
                    }

                    // Sync to shared _cubemap_alignment for skybox_override
                    _cubemap_alignment.rotation = fog.cubemap_rotation;
                    _cubemap_alignment.scale = fog.cubemap_scale;
                    _cubemap_alignment.offset = fog.cubemap_offset;
                    _cubemap_alignment.mip_scale = fog.cubemap_mip_scale;

                    postprocess.fog_params(fog);
                }

                // Skybox override
                if (ImGui::CollapsingHeader("Skybox Override")) {
                    auto skybox_params = skybox_override.params();

                    ImGui::Checkbox("Enable", &skybox_params.enabled);
                    ImGui::SetItemTooltip("Replace vanilla skybox with shaderpatch cubemap rendering.");

                    if (skybox_params.enabled) {
                        ImGui::Separator();
                        ImGui::Text("Cubemap Textures");

                        static char ground_cubemap_buf[256] = {};
                        static char sky_cubemap_buf[256] = {};
                        static bool buffers_initialized = false;

                        if (!buffers_initialized) {
                            strncpy(ground_cubemap_buf, skybox_params.ground_cubemap.c_str(), sizeof(ground_cubemap_buf) - 1);
                            strncpy(sky_cubemap_buf, skybox_params.sky_cubemap.c_str(), sizeof(sky_cubemap_buf) - 1);
                            buffers_initialized = true;
                        }

                        if (ImGui::InputText("GroundCubemap", ground_cubemap_buf, sizeof(ground_cubemap_buf))) {
                            skybox_params.ground_cubemap = ground_cubemap_buf;
                        }
                        ImGui::SetItemTooltip("Main sky cubemap texture name (ground level view).");

                        if (ImGui::InputText("SkyCubemap", sky_cubemap_buf, sizeof(sky_cubemap_buf))) {
                            skybox_params.sky_cubemap = sky_cubemap_buf;
                        }
                        ImGui::SetItemTooltip("Atmosphere/space cubemap for blending (optional).");

                        ImGui::Separator();
                        ImGui::Text("Sky Detection");

                        ImGui::DragFloat("Distance Threshold", &skybox_params.sky_distance_threshold, 100.0f, 100.0f, 100000.0f, "%.0f");
                        ImGui::SetItemTooltip("Distance beyond which pixels are considered sky. Increase if sky appears on terrain.");

                        const char* debug_modes[] = { "Off", "Show Depth", "Show Distance", "Stencil (Near)", "Stencil (Far)", "Raw Components" };
                        ImGui::Combo("Debug Mode", &skybox_params.debug_mode, debug_modes, 6);
                        ImGui::SetItemTooltip("Visualize depth/distance/stencil.\nStencil colors: 0=Black, 1=Red, 2=Green, 3=Blue, 4=Yellow, 5=Magenta, 6=Cyan, 7+=White\nRaw: R=near.x, G=near.y, B=far.x");

                        ImGui::Separator();
                        ImGui::Text("Atmosphere Blending (BF3 algorithm)");

                        ImGui::DragFloat("Atmos Density", &skybox_params.atmos_density, 0.001f, 0.0f, 0.1f, "%.4f");
                        ImGui::SetItemTooltip("Very small values (0.001-0.01). Controls atmosphere intensity.");

                        ImGui::DragFloat("Horizon Shift", &skybox_params.horizon_shift, 0.01f, 0.0f, 1.0f, "%.2f");
                        ImGui::SetItemTooltip("Push atmosphere lookup toward horizon.");

                        ImGui::DragFloat("Horizon Start", &skybox_params.horizon_start, 0.01f, 0.0f, 1.0f, "%.2f");
                        ImGui::SetItemTooltip("Where fade begins (vertical angle).");

                        ImGui::DragFloat("Horizon Blend", &skybox_params.horizon_blend, 0.01f, 0.0f, 1.0f, "%.2f");
                        ImGui::SetItemTooltip("0 = sharp ring, 1 = full coverage.");

                        ImGui::ColorEdit3("Tint", &skybox_params.tint.x);
                    }

                    skybox_override.params(skybox_params);
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Clouds")) {
                clouds.params(show_clouds_imgui(clouds.params()));

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

            if (ImGui::BeginTabItem("Debug Stencil")) {
                auto& params = debug_stencil.params();

                ImGui::Checkbox("Enabled", &params.enabled);
                ImGui::SetItemTooltip("Enable stencil buffer debug visualization");

                if (params.enabled) {
                    ImGui::Separator();

                    const char* modes[] = { "Depth", "Stencil (Color)", "Stencil (Raw)", "Combined" };
                    ImGui::Combo("Mode", &params.mode, modes, 4);
                    ImGui::SetItemTooltip(
                        "Depth: Visualize depth buffer (white = far plane)\n"
                        "Stencil (Color): Color-coded stencil values (0=Black, 1=Red, 2=Green, 3=Blue...)\n"
                        "Stencil (Raw): R=.x component, G=.y component (to see which has data)\n"
                        "Combined: Depth as brightness, stencil as hue");

                    ImGui::Checkbox("Use Near Buffer", &params.use_near);
                    ImGui::SetItemTooltip("true = nearscene depth-stencil, false = farscene");

                    ImGui::Separator();
                    ImGui::TextWrapped(
                        "If stencil modes show all black, either:\n"
                        "1. Stencil SRV creation failed\n"
                        "2. Game doesn't write stencil values\n"
                        "3. Wrong buffer selected (try toggling Near/Far)");
                }

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
                    { "R: %.3f", "G: %.3f", "B: %.3f" }, 0.025f,
                    -2.0f, 2.0f);
                ImGui::DragFloatFormatted3("Green", &params.channel_mix_green.x,
                    { "R: %.3f", "G: %.3f", "B: %.3f" }, 0.025f,
                    -2.0f, 2.0f);
                ImGui::DragFloatFormatted3("Blue", &params.channel_mix_blue.x,
                    { "R: %.3f", "G: %.3f", "B: %.3f" }, 0.025f,
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
                "Tonemapper", std::string{ to_string(params.tonemapper) },
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
                    [](const float v) { return eval_aces_srgb_fitted(glm::vec3{ v }).r; });
            }
            else if (params.tonemapper == Tonemapper::filmic_heji2015) {
                show_tonemapping_curve([whitepoint = params.filmic_heji_whitepoint](const float v) {
                    return eval_filmic_hejl2015(glm::vec3{ v }, whitepoint).x;
                    });

                ImGui::DragFloat("Whitepoint", &params.filmic_heji_whitepoint, 0.01f);
            }
            else if (params.tonemapper == Tonemapper::reinhard) {
                show_tonemapping_curve(
                    [](const float v) { return eval_reinhard(glm::vec3{ v }).x; });
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
                std::log(double{ params.f_stop }) / std::log(std::numbers::sqrt2_v<double>)));

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

        Fog_params show_fog_imgui(Fog_params params) noexcept
        {
            ImGui::Checkbox("Enabled", &params.enabled);

            ImGui::ColorEdit3("Color", &params.color.x);
            ImGui::SetItemTooltip("Base fog color used by all fog types.");

            ImGui::DragFloat("Start Distance", &params.start_distance, 1.0f, 0.0f, 1000.0f);
            ImGui::SetItemTooltip("Distance where fog begins. No fog closer than this.");

            ImGui::DragFloat("Max Opacity", &params.max_opacity, 0.01f, 0.0f, 1.0f);
            ImGui::SetItemTooltip("Maximum fog contribution (0-1). Prevents complete whiteout.");

            ImGui::Separator();
            ImGui::Text("Height Bounds");

            ImGui::DragFloat("Ground Height", &params.height_base, 5.0f, -500.0f, 500.0f);
            ImGui::SetItemTooltip("Y level where fog is at full strength.\nTypically ground level (0).");

            // Note: BF3 uses implicit height modulation via height_base (atmosheight), not an explicit ceiling

            ImGui::Separator();
            ImGui::Text("Distance Fog Mode");
            ImGui::SetItemTooltip("Choose ONE: Standard (linear) or Atmosphere (sqrt falloff).");

            // Radio buttons for mutually exclusive distance fog modes
            bool use_standard = !params.atmosphere_enabled;
            bool use_atmosphere = params.atmosphere_enabled;

            if (ImGui::RadioButton("Standard (Linear)", use_standard)) {
                params.atmosphere_enabled = false;
            }
            ImGui::SetItemTooltip("Simple linear fog from Start to End distance.\nBest for indoor areas or simple scenes.");

            ImGui::SameLine();

            if (ImGui::RadioButton("Atmosphere (SWBF3)", use_atmosphere)) {
                params.atmosphere_enabled = true;
            }
            ImGui::SetItemTooltip("Sqrt distance falloff with horizon plane.\nBest for outdoor ground-to-space transitions.");

            // Show controls for the selected mode
            if (!params.atmosphere_enabled) {
                // Standard fog controls
                ImGui::DragFloat("Density", &params.density, 0.1f, 0.0f, 10.0f);
                ImGui::SetItemTooltip("Linear fog density. Higher = thicker fog.");

                ImGui::DragFloat("End Distance", &params.end_distance, 10.0f, params.start_distance, 5000.0f);
                ImGui::SetItemTooltip("Distance where fog reaches full density.");
            }
            else {
                // BF3 Atmosphere controls (Stage 1: cubemap-based atmosphere)
                ImGui::DragFloat("Intensity", &params.atmos_intensity, 0.0001f, 0.0f, 0.01f, "%.4f");
                ImGui::SetItemTooltip("BF3 atmosdata.x - Atmosphere intensity.\nVery small values like 0.0001-0.001 for subtle, 0.001-0.005 for heavy.");

                ImGui::DragFloat("Falloff", &params.atmos_falloff, 0.05f, 0.1f, 2.0f);
                ImGui::SetItemTooltip("Distance curve exponent:\n0.5 = sqrt (BF3 default)\n1.0 = linear\n2.0 = quadratic");

                ImGui::DragFloat("Horizon Offset", &params.horizon_offset, 0.01f, 0.0f, 0.5f);
                ImGui::SetItemTooltip("Shifts cubemap lookup toward horizon.\nAdds more sky color at eye level.");

                // BF3 height blending
                ImGui::Separator();
                ImGui::Text("Height Blending (BF3 atmosdata)");
                ImGui::DragFloat("High Alt Intensity", &params.atmos_high_intensity, 0.0001f, 0.0f, 0.01f, "%.4f");
                ImGui::SetItemTooltip("BF3 atmosdata.z - Atmosphere at high altitude.\nBlends toward this value as height increases.");

                ImGui::DragFloat("Height Blend Weight", &params.height_blend_weight, 0.1f, 0.0f, 10.0f);
                ImGui::SetItemTooltip("BF3 atmosdata.w - Weight for height blending.\n0 = no blending, higher = faster blend to high alt value.");

                ImGui::Separator();
                ImGui::DragFloat("Zenith Haze", &params.zenith_haze, 0.01f, 0.0f, 1.0f);
                ImGui::SetItemTooltip("Atmospheric haze when looking up at the sky.\n0 = clear sky, 0.3 = light haze, 1 = thick atmosphere.");

                ImGui::Separator();
                ImGui::Text("Stage 2: Fog Color Overlay");
                ImGui::DragFloat("Fog Density", &params.density, 0.1f, 0.0f, 10.0f);
                ImGui::SetItemTooltip("Overlays fog color on top of atmosphere.\n0 = cubemap only, higher = more fog color.");

                ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "Cubemaps set in Environment tab");
            }

            ImGui::Separator();
            ImGui::Text("Ground Fog (Optional)");

            ImGui::Checkbox("Extra Ground Fog", &params.height_fog_enabled);
            ImGui::SetItemTooltip("Adds extra fog density below Ground Height.\nUseful for swamps, valleys, low-lying mist.");

            if (params.height_fog_enabled) {
                ImGui::DragFloat("Ground Fog Density", &params.height_density, 0.1f, 0.0f, 10.0f);
                ImGui::SetItemTooltip("Extra fog density below Ground Height.");
            }

            ImGui::Separator();
            ImGui::Text("Effects");

            ImGui::Checkbox("Sun Inscatter", &params.sun_inscatter_enabled);
            ImGui::SetItemTooltip("Brighter fog when looking toward the sun.\nUses map sun automatically.");

            if (params.sun_inscatter_enabled) {
                ImGui::DragFloat("Sun Intensity", &params.sun_intensity, 0.1f, 0.0f, 10.0f);
                ImGui::SetItemTooltip("Strength of the inscattering glow.\nTry 1-3 for subtle, 3-6 for dramatic.");

                ImGui::DragFloat("Sun Power", &params.sun_power, 1.0f, 1.0f, 128.0f);
                ImGui::SetItemTooltip("Falloff exponent.\nLower = wider glow, Higher = tighter sun disk.");
            }

            ImGui::Checkbox("Noise", &params.noise_enabled);
            ImGui::SetItemTooltip("Animated noise to break up uniform fog.");

            if (params.noise_enabled) {
                ImGui::DragFloat("Noise Scale", &params.noise_scale, 1.0f, 10.0f, 500.0f);
                ImGui::SetItemTooltip("World units per noise tile.");

                ImGui::DragFloat("Noise Intensity", &params.noise_intensity, 0.01f, 0.0f, 1.0f);
                ImGui::SetItemTooltip("How much noise affects density.");

                ImGui::DragFloat("Noise Speed", &params.noise_speed, 0.01f, 0.0f, 1.0f);
                ImGui::SetItemTooltip("Animation speed.");
            }

            ImGui::Separator();
            if (ImGui::CollapsingHeader("Debug: Depth Linearization")) {
                ImGui::Checkbox("Enable Debug Visualization", &params.debug_depth_enabled);
                ImGui::SetItemTooltip("Shows depth linearization as colors.\nGreen=close, Red=far.");

                if (params.debug_depth_enabled) {
                    const char* buffer_modes[] = { "Min(Both)", "Near Buffer Only", "Far Buffer Only", "Show Which Wins" };
                    ImGui::Combo("Buffer Mode", &params.debug_buffer_mode, buffer_modes, 4);
                    ImGui::SetItemTooltip("Min(Both) = normal operation\nNear/Far Only = isolate each buffer\nShow Which Wins = cyan=near, magenta=far");

                    ImGui::DragFloat("Max Distance", &params.debug_max_distance, 10.0f, 100.0f, 10000.0f);
                    ImGui::SetItemTooltip("Distance that maps to full red.");

                    ImGui::Separator();
                    ImGui::Text("Manual Override Values");
                    ImGui::Text("Near Scene Projection");
                    ImGui::DragFloat("Near Plane##near", &params.near_scene_near, 1.0f, 0.1f, 1000.0f);
                    ImGui::DragFloat("Far Plane##near", &params.near_scene_far, 1.0f, 1.0f, 2000.0f);

                    ImGui::Text("Far Scene Projection");
                    ImGui::DragFloat("Near Plane##far", &params.far_scene_near, 10.0f, 1.0f, 5000.0f);
                    ImGui::DragFloat("Far Plane##far", &params.far_scene_far, 100.0f, 100.0f, 50000.0f);

                    ImGui::Separator();
                    ImGui::Text("Captured Projection Values (read-only)");
                    ImGui::Text("Near Buffer: m33=%.4f, m43=%.4f", params.captured_near_m33, params.captured_near_m43);
                    ImGui::Text("Far Buffer:  m33=%.4f, m43=%.4f", params.captured_far_m33, params.captured_far_m43);

                    // Compute what near/far planes these correspond to
                    // m33 = far / (far - near), m43 = -near * far / (far - near)
                    // near = -m43 / m33, far = m43 / (m33 - 1)
                    if (params.captured_near_m33 != 0.0f && params.captured_near_m33 != 1.0f) {
                        float near_near = -params.captured_near_m43 / params.captured_near_m33;
                        float near_far = params.captured_near_m43 / (params.captured_near_m33 - 1.0f);
                        ImGui::Text("Near Buffer planes: near=%.1f, far=%.1f", near_near, near_far);
                    }
                    if (params.captured_far_m33 != 0.0f && params.captured_far_m33 != 1.0f) {
                        float far_near = -params.captured_far_m43 / params.captured_far_m33;
                        float far_far = params.captured_far_m43 / (params.captured_far_m33 - 1.0f);
                        ImGui::Text("Far Buffer planes:  near=%.1f, far=%.1f", far_near, far_far);
                    }
                }
            }

            return params;
        }

        Cloud_params show_clouds_imgui(Cloud_params params) noexcept
        {
            ImGui::Checkbox("Enabled", &params.enabled);

            ImGui::Separator();
            ImGui::Text("Cloud Layers");
            ImGui::SetItemTooltip("Configure up to 3 cloud layers at different heights.");

            const char* layer_names[] = { "Layer 0 (Low/Dense)", "Layer 1 (Mid)", "Layer 2 (High/Wispy)" };

            for (int i = 0; i < 3; ++i) {
                auto& layer = params.layers[i];

                if (ImGui::TreeNode(layer_names[i])) {
                    ImGui::Checkbox("Enabled##layer", &layer.enabled);

                    if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::DragFloat("Height", &layer.height, 10.0f, 0.0f, 2000.0f);
                        ImGui::SetItemTooltip("World Y coordinate of this cloud layer.");

                        ImGui::DragFloat("Thickness", &layer.thickness, 5.0f, 10.0f, 200.0f);
                        ImGui::SetItemTooltip("Vertical extent for fog transition.");

                        ImGui::DragFloat("Scale", &layer.scale, 0.00001f, 0.0001f, 0.01f, "%.5f");
                        ImGui::SetItemTooltip("UV scale for noise sampling. Smaller = larger clouds.");

                        ImGui::DragFloat("Curvature", &layer.curvature, 0.0000001f, 0.0f, 0.001f, "%.7f");
                        ImGui::SetItemTooltip("Curves layer toward horizon.\n0 = flat plane, higher = more curve.\nTry 0.0000001 for subtle planetary curvature.");
                    }

                    if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::DragFloat("Scroll Speed", &layer.scroll_speed, 0.001f, 0.0f, 0.5f, "%.4f");
                        ImGui::SetItemTooltip("Wind animation speed. Keep very low (0.01-0.03).");

                        ImGui::DragFloat("Scroll Angle", &layer.scroll_angle, 1.0f, 0.0f, 360.0f);
                        ImGui::SetItemTooltip("Wind direction in degrees.");
                    }

                    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::DragFloat("Density", &layer.density, 0.01f, 0.0f, 1.0f);
                        ImGui::SetItemTooltip("Visual opacity of this layer.");

                        ImGui::DragFloat("Threshold", &layer.cloud_threshold, 0.01f, 0.0f, 1.0f);
                        ImGui::SetItemTooltip("Noise threshold for cloud formation.\nHigher = less cloud coverage.");

                        ImGui::DragFloat("Softness", &layer.cloud_softness, 0.01f, 0.01f, 1.0f);
                        ImGui::SetItemTooltip("Edge softness of clouds.");
                    }

                    if (ImGui::CollapsingHeader("Octave Weights (BF3-style)")) {
                        ImGui::DragFloat4("Weights (RGBA)", &layer.octave_weights.x, 0.01f, -1.0f, 1.0f);
                        ImGui::SetItemTooltip("R=large shapes, G=medium, B=fine, A=edge variation.\nNegative values subtract that octave (erodes edges).");

                        ImGui::DragFloat4("Blend (RGBA)", &layer.octave_blend.x, 0.01f, 0.0f, 1.0f);
                        ImGui::SetItemTooltip("Crossfade between primary and secondary sample per channel.\n0 = primary only, 1 = secondary only.");
                    }

                    if (ImGui::CollapsingHeader("Lighting")) {
                        ImGui::Checkbox("Use Normal Lighting", &layer.use_normal_lighting);
                        ImGui::SetItemTooltip("OFF = BF3-style noise self-shadow (simpler).\nON = Gradient-based normal lighting (bumpier).");

                        ImGui::ColorEdit3("Lit Color", &layer.color_lit.x);
                        ImGui::SetItemTooltip("Color of clouds facing the sun.");

                        ImGui::ColorEdit3("Dark Color", &layer.color_dark.x);
                        ImGui::SetItemTooltip("Color of clouds facing away from sun.");

                        ImGui::DragFloat("Sun Color Influence", &layer.sun_color_influence, 0.01f, 0.0f, 1.0f);
                        ImGui::SetItemTooltip("How much the map sun color affects this layer.");

                        ImGui::DragFloat("Lighting Wrap", &layer.lighting_wrap, 0.01f, 0.0f, 1.0f);
                        ImGui::SetItemTooltip("Wraps lighting around clouds.\n0 = harsh, 1 = fully wrapped.");

                        ImGui::DragFloat("Brightness", &layer.cloud_brightness, 0.01f, 0.0f, 2.0f);
                        ImGui::SetItemTooltip("Overall cloud brightness multiplier.");

                        ImGui::DragFloat("Min Brightness", &layer.min_brightness, 0.01f, 0.0f, 1.0f);
                        ImGui::SetItemTooltip("Minimum brightness floor to prevent fully dark clouds.");
                    }

                    if (ImGui::CollapsingHeader("Fog Integration")) {
                        ImGui::DragFloat("Fog Boost Max", &layer.fog_boost_max, 0.05f, 0.0f, 1.0f);
                        ImGui::SetItemTooltip("Max fog intensity when inside this layer.\n0 = no fog, 1 = full whiteout.");
                    }

                    ImGui::TreePop();
                }
            }

            ImGui::Separator();
            ImGui::Text("Global Settings");

            if (ImGui::CollapsingHeader("Fading", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat("Horizon Fade Start", &params.horizon_fade_start, 0.01f, 0.0f, 0.5f);
                ImGui::SetItemTooltip("Start fading clouds near horizon.");

                ImGui::DragFloat("Horizon Fade End", &params.horizon_fade_end, 0.001f, 0.0f, 0.2f);
                ImGui::SetItemTooltip("Fully fade clouds at horizon.");

                ImGui::DragFloat("Distance Fade Start", &params.distance_fade_start, 100.0f, 0.0f, 20000.0f);
                ImGui::SetItemTooltip("Start fading clouds at this distance.");

                ImGui::DragFloat("Distance Fade End", &params.distance_fade_end, 100.0f, params.distance_fade_start, 50000.0f);
                ImGui::SetItemTooltip("Fully fade clouds at this distance.");

                ImGui::DragFloat("Near Fade Start", &params.near_fade_start, 1.0f, 0.0f, 500.0f);
                ImGui::SetItemTooltip("Start fading clouds at this distance from camera.");

                ImGui::DragFloat("Near Fade End", &params.near_fade_end, 1.0f, 0.0f, 100.0f);
                ImGui::SetItemTooltip("Fully fade clouds at this distance (when flying through).");
            }

            if (ImGui::CollapsingHeader("Fog Integration (Shared)")) {
                ImGui::DragFloat("Global Fog Boost Scale", &params.global_fog_boost_scale, 0.05f, 0.0f, 2.0f);
                ImGui::SetItemTooltip("Multiplier for all layer fog boosts.");
            }

            if (ImGui::CollapsingHeader("Curvature")) {
                ImGui::DragFloat2("Curvature Center (XZ)", &params.curvature_center.x, 10.0f);
                ImGui::SetItemTooltip("World XZ coordinates for curvature center.\nClouds curve down away from this point.");
            }

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
    }
}
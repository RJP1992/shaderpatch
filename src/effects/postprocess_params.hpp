#pragma once

#include "glm_yaml_adapters.hpp"

#include <glm/glm.hpp>
#include <string>
#include <string_view>

#pragma warning(push)
#pragma warning(disable : 4996)
#pragma warning(disable : 4127)

#include <yaml-cpp/yaml.h>
#pragma warning(pop)

namespace sp::effects {

    enum class Hdr_state { hdr, stock };

    enum class Tonemapper { filmic, aces_fitted, filmic_heji2015, reinhard, none };

    enum class Bloom_mode { blended, threshold };

    enum class SSAO_mode { ambient, global };

    enum class SSAO_method { assao };

    // Shared cubemap alignment parameters - used by fog, sky, atmosphere systems
    struct Cubemap_alignment {
        glm::vec3 rotation = {0.0f, 0.0f, 0.0f};  // Euler angles (pitch, yaw, roll) in degrees
        glm::vec3 scale = {1.0f, 1.0f, 1.0f};     // Per-axis scale
        glm::vec3 offset = {0.0f, 0.0f, 0.0f};    // Direction offset
        float mip_scale = 0.0f;                    // Blur cubemap for close geometry
    };

    // Skybox override parameters - replaces vanilla skybox with post-process cubemap rendering
    struct Skybox_override_params {
        bool enabled = false;

        // Cubemap texture names (loaded by texture manager)
        std::string ground_cubemap;         // Main sky cubemap (ground level view)
        std::string sky_cubemap;            // Atmosphere/space cubemap for blending

        // Sky detection
        float sky_distance_threshold = 5000.0f;  // Distance beyond which pixels are considered sky

        // Atmosphere blending (BF3 algorithm)
        float atmos_density = 0.005f;       // Very small values (0.001-0.01)
        float horizon_shift = 0.1f;         // Push lookup toward horizon
        float horizon_start = 0.3f;         // Where fade begins (vertical angle)
        float horizon_blend = 0.0f;         // 0 = sharp ring, 1 = full coverage

        // Color tint
        glm::vec3 tint = {1.0f, 1.0f, 1.0f};

        // Debug mode: 0=off, 1=show depth values, 2=show distance values
        int debug_mode = 0;
    };

    struct Bloom_params {
        bool enabled = true;
        Bloom_mode mode = Bloom_mode::blended;

        float threshold = 1.0f;
        float blend_factor = 0.05f;

        float intensity = 1.0f;
        glm::vec3 tint{ 1.0f, 1.0f, 1.0f };

        float inner_scale = 1.0f;
        glm::vec3 inner_tint{ 1.0f, 1.0f, 1.0f };

        float inner_mid_scale = 1.0f;
        glm::vec3 inner_mid_tint{ 1.0f, 1.0f, 1.0f };

        float mid_scale = 1.0f;
        glm::vec3 mid_tint{ 1.0f, 1.0f, 1.0f };

        float outer_mid_scale = 1.0f;
        glm::vec3 outer_mid_tint{ 1.0f, 1.0f, 1.0f };

        float outer_scale = 1.0f;
        glm::vec3 outer_tint{ 1.0f, 1.0f, 1.0f };

        bool use_dirt = false;
        float dirt_scale = 1.0f;
        glm::vec3 dirt_tint{ 1.0f, 1.0f, 1.0f };
        std::string dirt_texture_name;
    };

    struct Vignette_params {
        bool enabled = true;

        float end = 1.0f;
        float start = 0.25f;
    };

    struct Color_grading_params {
        glm::vec3 color_filter = { 1.0f, 1.0f, 1.0f };
        float saturation = 1.0f;
        float exposure = 0.0f;
        float brightness = 1.0f;
        float contrast = 1.0f;

        Tonemapper tonemapper = Tonemapper::filmic;

        float filmic_toe_strength = 0.0f;
        float filmic_toe_length = 0.5f;
        float filmic_shoulder_strength = 0.0f;
        float filmic_shoulder_length = 0.5f;
        float filmic_shoulder_angle = 0.0f;

        float filmic_heji_whitepoint = 1.0f;

        glm::vec3 shadow_color = { 1.0f, 1.0f, 1.0f };
        glm::vec3 midtone_color = { 1.0f, 1.0f, 1.0f };
        glm::vec3 highlight_color = { 1.0f, 1.0f, 1.0f };

        float shadow_offset = 0.0f;
        float midtone_offset = 0.0f;
        float highlight_offset = 0.0f;

        float hsv_hue_adjustment = 0.0f;
        float hsv_saturation_adjustment = 1.0f;
        float hsv_value_adjustment = 1.0f;

        glm::vec3 channel_mix_red{ 1.0f, 0.0f, 0.0f };
        glm::vec3 channel_mix_green{ 0.0f, 1.0f, 0.0f };
        glm::vec3 channel_mix_blue{ 0.0f, 0.0f, 1.0f };
    };

    struct Film_grain_params {
        bool enabled = false;
        bool colored = false;

        float amount = 0.035f;
        float size = 1.65f;
        float color_amount = 0.6f;
        float luma_amount = 1.0f;
    };

    struct SSAO_params {
        bool enabled = true;

        SSAO_mode mode = SSAO_mode::ambient;
        SSAO_method method = SSAO_method::assao;

        float radius = 1.5f;
        float shadow_multiplier = 0.75f;
        float shadow_power = 0.75f;
        float detail_shadow_strength = 0.5f;
        int blur_pass_count = 2;
        float sharpness = 0.98f;
    };

    struct FFX_cas_params {
        bool enabled = false;

        float sharpness = 0.0f;
    };

    struct DOF_params {
        bool enabled = false;

        float film_size_mm = 35.0f;
        float focus_distance = 10.0f;
        float f_stop = 16.0f;
    };

    struct Fog_params {
        bool enabled = false;

        // Base fog (BF3: fogcolour)
        glm::vec3 color = { 0.7f, 0.8f, 0.9f };
        float density = 1.0f;                   // BF3: fogcolour.w
        float start_distance = 50.0f;
        float end_distance = 500.0f;

        // Height fog
        bool height_fog_enabled = false;
        float height_density = 1.0f;
        float height_base = 0.0f;               // BF3: atmosdata.y - atmosphere height reference
        float height_falloff = 0.01f;

        // Sun inscattering
        bool sun_inscatter_enabled = false;
        glm::vec3 sun_direction = { 0.5f, 0.5f, 0.0f };
        glm::vec3 sun_color = { 1.0f, 0.9f, 0.7f };
        float sun_intensity = 0.5f;
        float sun_power = 8.0f;

        float max_opacity = 0.95f;

        // Noise
        bool noise_enabled = false;
        float noise_scale = 100.0f;
        float noise_intensity = 0.3f;
        float noise_speed = 0.1f;

        // BF3 Atmosphere (exact algorithm from SWBF3 shaders)
        // Two-stage system: atmosphere first (cubemap), then fog (constant color)
        bool atmosphere_enabled = false;
        float atmos_intensity = 0.001f;         // BF3: atmosdata.x - multiplied by pow(dist, falloff)
        float atmos_falloff = 0.5f;             // Distance exponent: 0.5=sqrt (BF3 default), 1.0=linear
        float horizon_offset = 0.1f;            // Shifts cubemap lookup toward horizon
        std::string atmosphere_texture_name;    // Optional cubemap for sky color (empty = use fog_color)

        // BF3 height-based atmosphere blending
        // BF3: atmos = lerp(atmos, atmosdata.z, saturate(heightdiff * atmosdata.w))
        float atmos_high_intensity = 0.0f;      // BF3: atmosdata.z - atmosphere at high altitude
        float height_blend_weight = 0.0f;       // BF3: atmosdata.w - weight for height blending

        // Cubemap alignment transform (for matching SWBF2 skyboxes)
        glm::vec3 cubemap_rotation = {0.0f, 0.0f, 0.0f};  // Euler angles in degrees (pitch, yaw, roll)
        glm::vec3 cubemap_scale = {1.0f, 1.0f, 1.0f};     // Per-axis scale
        glm::vec3 cubemap_offset = {0.0f, 0.0f, 0.0f};    // Direction offset
        float cubemap_mip_scale = 0.0f;                    // Blur cubemap for close geometry (prevents sun bleed)

        // Cubemap debug visualizer
        bool cubemap_debug_enabled = false;
        bool cubemap_debug_at_infinity = true;  // false = fixed world distance
        float cubemap_debug_distance = 5000.0f; // Distance when not at infinity

        // Cloud integration (set by clouds system before postprocess)
        float cloud_boost = 0.0f;               // 0-1, fog density boost when inside cloud
        glm::vec3 cloud_tint = {1.0f, 1.0f, 1.0f};  // Fog color tint when inside cloud

        // Zenith (sky) haze - adds atmospheric haze when looking up
        float zenith_haze = 0.3f;               // 0 = clear sky, 1 = very hazy atmosphere

        // BF3-style space cubemap blending
        // Blends between atmosphere_texture and space_texture based on altitude
        std::string space_texture_name;         // Space/stars cubemap (empty = disabled)
        float altitude_blend_start = 1000.0f;   // Altitude where space starts fading in
        float altitude_blend_end = 5000.0f;     // Altitude where space is 100%
        float sky_blend_override = 0.0f;        // Manual override: 0=auto altitude, >0=manual blend

        // Debug: depth linearization visualization
        bool debug_depth_enabled = false;
        int debug_buffer_mode = 0;              // 0=min(both), 1=near only, 2=far only, 3=show which wins
        float debug_max_distance = 1000.0f;     // Max distance for color visualization
        float near_scene_near = 220.0f;         // Near scene near plane
        float near_scene_far = 300.0f;          // Near scene far plane
        float far_scene_near = 500.0f;          // Far scene near plane
        float far_scene_far = 5000.0f;          // Far scene far plane

        // Read-only: actual captured projection values (set by postprocess)
        float captured_near_m33 = 0.0f;
        float captured_near_m43 = 0.0f;
        float captured_far_m33 = 0.0f;
        float captured_far_m43 = 0.0f;
    };

    struct Sky_dome_params {
        bool enabled = false;

        // Atmosphere appearance
        std::string atmosphere_texture_name;    // Cubemap for atmosphere colors
        float atmosphere_density = 1.0f;        // Overall intensity

        // Horizon ring control (BF3 atmoshorizon equivalent)
        float horizon_shift = 0.1f;             // How much to push lookup toward horizon
        float horizon_start = 0.3f;             // Where fade begins (vertical angle)
        float horizon_blend = 0.0f;             // 0 = sharp ring, 1 = full coverage

        // Height fade (ground-to-space transition)
        float fade_start_height = 100.0f;       // Start fading in
        float fade_end_height = 500.0f;         // Fully visible

        // Color tint
        glm::vec3 tint = {1.0f, 1.0f, 1.0f};

        // Cubemap alignment transform (shared with fog system)
        glm::vec3 cubemap_rotation = {0.0f, 0.0f, 0.0f};
        glm::vec3 cubemap_scale = {1.0f, 1.0f, 1.0f};
        glm::vec3 cubemap_offset = {0.0f, 0.0f, 0.0f};
    };

    // BF3-style sky rendering modes
    enum class Sky_bf3_mode {
        basic,        // sky_bf3.fx - simple cubemap
        atmospheric,  // sky_atmos_bf3.fx - with atmosphere
        blended       // skyblend_bf3.fx - ground/space transition
    };

    struct Sky_bf3_params {
        bool enabled = false;
        Sky_bf3_mode mode = Sky_bf3_mode::atmospheric;

        // Atmosphere parameters (BF3 atmoshorizon equivalent)
        float atmos_density = 0.005f;       // Very small values (0.001-0.01)
        float horizon_shift = 0.1f;         // atmoshorizon[0] - push toward horizon
        float horizon_start = 0.3f;         // atmoshorizon[1] - where fade begins
        float horizon_blend = 0.0f;         // atmoshorizon[2] - 0=ring, 1=full coverage

        // Space blend parameters (for blended mode)
        float blend_start_height = 500.0f;  // Height where blend begins
        float blend_end_height = 2000.0f;   // Height where fully in space
        float manual_blend = -1.0f;         // -1 = auto from height, 0-1 = manual

        // Cubemap textures
        std::string ground_cubemap;         // envmap - ground sky
        std::string space_cubemap;          // spaceenvmap - space sky
        std::string atmosphere_cubemap;     // atmoscubemap - atmosphere colors

        // Cubemap alignment transform (shared with fog system)
        glm::vec3 cubemap_rotation = {0.0f, 0.0f, 0.0f};
        glm::vec3 cubemap_scale = {1.0f, 1.0f, 1.0f};
        glm::vec3 cubemap_offset = {0.0f, 0.0f, 0.0f};

        // Visual options
        bool monochrome = false;
        bool use_rgbe = false;              // Use RGBE decoding for HDR cubemaps
        glm::vec3 ambient_color = {1.0f, 1.0f, 1.0f};
        glm::vec3 tint = {1.0f, 1.0f, 1.0f};
    };

    inline auto to_string(const Sky_bf3_mode mode) noexcept
    {
        using namespace std::literals;

        switch (mode) {
        case Sky_bf3_mode::basic:
            return "Basic"s;
        case Sky_bf3_mode::atmospheric:
            return "Atmospheric"s;
        case Sky_bf3_mode::blended:
            return "Blended"s;
        }

        std::terminate();
    }

    inline auto sky_bf3_mode_from_string(const std::string_view string) noexcept
    {
        if (string == to_string(Sky_bf3_mode::basic))
            return Sky_bf3_mode::basic;
        else if (string == to_string(Sky_bf3_mode::atmospheric))
            return Sky_bf3_mode::atmospheric;
        else if (string == to_string(Sky_bf3_mode::blended))
            return Sky_bf3_mode::blended;

        return Sky_bf3_mode::atmospheric;
    }

    inline auto to_string(const Tonemapper tonemapper) noexcept
    {
        using namespace std::literals;

        switch (tonemapper) {
        case Tonemapper::filmic:
            return "Filmic"s;
        case Tonemapper::aces_fitted:
            return "ACES sRGB Fitted"s;
        case Tonemapper::filmic_heji2015:
            return "Filmic Heji 2015"s;
        case Tonemapper::reinhard:
            return "Reinhard"s;
        case Tonemapper::none:
            return "None"s;
        }

        std::terminate();
    }

    inline auto tonemapper_from_string(const std::string_view string) noexcept
    {
        if (string == to_string(Tonemapper::filmic))
            return Tonemapper::filmic;
        else if (string == to_string(Tonemapper::aces_fitted))
            return Tonemapper::aces_fitted;
        else if (string == to_string(Tonemapper::filmic_heji2015))
            return Tonemapper::filmic_heji2015;
        else if (string == to_string(Tonemapper::reinhard))
            return Tonemapper::reinhard;
        else if (string == to_string(Tonemapper::none))
            return Tonemapper::none;

        return Tonemapper::filmic;
    }

    inline auto to_string(const Bloom_mode bloom_mode) noexcept
    {
        using namespace std::literals;

        switch (bloom_mode) {
        case Bloom_mode::blended:
            return "Blended"s;
        case Bloom_mode::threshold:
            return "Threshold"s;
        }

        std::terminate();
    }

    inline auto bloom_mode_from_string(const std::string_view string) noexcept
    {
        if (string == to_string(Bloom_mode::blended))
            return Bloom_mode::blended;
        else if (string == to_string(Bloom_mode::threshold))
            return Bloom_mode::threshold;

        return Bloom_mode::blended;
    }

    inline auto to_string(const SSAO_mode ssao_mode) noexcept
    {
        using namespace std::literals;

        switch (ssao_mode) {
        case SSAO_mode::ambient:
            return "Ambient"s;
        case SSAO_mode::global:
            return "Global"s;
        }

        std::terminate();
    }

    inline auto ssao_mode_from_string(const std::string_view string) noexcept
    {
        if (string == to_string(SSAO_mode::ambient))
            return SSAO_mode::ambient;
        else if (string == to_string(SSAO_mode::global))
            return SSAO_mode::global;

        return SSAO_mode::ambient;
    }

    inline auto to_string(const SSAO_method ssao_type) noexcept
    {
        using namespace std::literals;

        switch (ssao_type) {
        case SSAO_method::assao:
            return "ASSAO"s;
        }

        std::terminate();
    }

    inline auto ssao_type_from_string(const std::string_view string) noexcept
    {
        if (string == to_string(SSAO_method::assao)) return SSAO_method::assao;

        return SSAO_method::assao;
    }

}

namespace YAML {

    template<>
    struct convert<sp::effects::Tonemapper> {
        static Node encode(const sp::effects::Tonemapper tonemapper)
        {
            return YAML::Node{ to_string(tonemapper) };
        }

        static bool decode(const Node& node, sp::effects::Tonemapper& tonemapper)
        {
            tonemapper = sp::effects::tonemapper_from_string(node.as<std::string>());

            return true;
        }
    };

    template<>
    struct convert<sp::effects::Bloom_mode> {
        static Node encode(const sp::effects::Bloom_mode bloom_mode)
        {
            return YAML::Node{ to_string(bloom_mode) };
        }

        static bool decode(const Node& node, sp::effects::Bloom_mode& bloom_mode)
        {
            bloom_mode = sp::effects::bloom_mode_from_string(node.as<std::string>());

            return true;
        }
    };

    template<>
    struct convert<sp::effects::SSAO_mode> {
        static Node encode(const sp::effects::SSAO_mode ssao_mode)
        {
            return YAML::Node{ to_string(ssao_mode) };
        }

        static bool decode(const Node& node, sp::effects::SSAO_mode& ssao_mode)
        {
            ssao_mode = sp::effects::ssao_mode_from_string(node.as<std::string>());

            return true;
        }
    };

    template<>
    struct convert<sp::effects::SSAO_method> {
        static Node encode(const sp::effects::SSAO_method ssao_type)
        {
            return YAML::Node{ to_string(ssao_type) };
        }

        static bool decode(const Node& node, sp::effects::SSAO_method& ssao_type)
        {
            ssao_type = sp::effects::ssao_type_from_string(node.as<std::string>());

            return true;
        }
    };

    template<>
    struct convert<sp::effects::Bloom_params> {
        static Node encode(const sp::effects::Bloom_params& params)
        {
            using namespace std::literals;

            YAML::Node node;

            node["Enable"s] = params.enabled;
            node["Mode"s] = params.mode;

            node["BlendFactor"s] = params.blend_factor;
            node["Threshold"s] = params.threshold;
            node["Intensity"s] = params.intensity;

            node["Tint"s].push_back(params.tint.r);
            node["Tint"s].push_back(params.tint.g);
            node["Tint"s].push_back(params.tint.b);

            node["InnerScale"s] = params.inner_scale;
            node["InnerTint"s].push_back(params.inner_tint.r);
            node["InnerTint"s].push_back(params.inner_tint.g);
            node["InnerTint"s].push_back(params.inner_tint.b);

            node["InnerMidScale"s] = params.inner_mid_scale;
            node["InnerMidTint"s].push_back(params.inner_mid_tint.r);
            node["InnerMidTint"s].push_back(params.inner_mid_tint.g);
            node["InnerMidTint"s].push_back(params.inner_mid_tint.b);

            node["MidScale"s] = params.mid_scale;
            node["MidTint"s].push_back(params.mid_tint.r);
            node["MidTint"s].push_back(params.mid_tint.g);
            node["MidTint"s].push_back(params.mid_tint.b);

            node["OuterMidScale"s] = params.outer_mid_scale;
            node["OuterMidTint"s].push_back(params.outer_mid_tint.r);
            node["OuterMidTint"s].push_back(params.outer_mid_tint.g);
            node["OuterMidTint"s].push_back(params.outer_mid_tint.b);

            node["OuterScale"s] = params.outer_scale;
            node["OuterTint"s].push_back(params.outer_tint.r);
            node["OuterTint"s].push_back(params.outer_tint.g);
            node["OuterTint"s].push_back(params.outer_tint.b);

            node["UseDirt"s] = params.use_dirt;
            node["DirtScale"s] = params.dirt_scale;
            node["DirtTint"s].push_back(params.dirt_tint[0]);
            node["DirtTint"s].push_back(params.dirt_tint[1]);
            node["DirtTint"s].push_back(params.dirt_tint[2]);
            node["DirtTextureName"s] = params.dirt_texture_name;

            return node;
        }

        static bool decode(const Node& node, sp::effects::Bloom_params& params)
        {
            using namespace std::literals;

            params = sp::effects::Bloom_params{};

            params.enabled = node["Enable"s].as<bool>(params.enabled);
            params.mode =
                node["Mode"s].as<sp::effects::Bloom_mode>(sp::effects::Bloom_mode::threshold);

            if (params.mode == sp::effects::Bloom_mode::blended)
                params.blend_factor = node["BlendFactor"s].as<float>(params.blend_factor);
            else
                params.threshold = node["Threshold"s].as<float>(params.threshold);

            params.intensity = node["Intensity"s].as<float>(0.75f);

            params.tint[0] = node["Tint"s][0].as<float>(params.tint[0]);
            params.tint[1] = node["Tint"s][1].as<float>(params.tint[1]);
            params.tint[2] = node["Tint"s][2].as<float>(params.tint[2]);

            params.inner_scale = node["InnerScale"s].as<float>(params.inner_scale);
            params.inner_tint[0] = node["InnerTint"s][0].as<float>(params.inner_tint[0]);
            params.inner_tint[1] = node["InnerTint"s][1].as<float>(params.inner_tint[1]);
            params.inner_tint[2] = node["InnerTint"s][2].as<float>(params.inner_tint[2]);

            params.inner_mid_scale =
                node["InnerMidScale"s].as<float>(params.inner_mid_scale);
            params.inner_mid_tint[0] =
                node["InnerMidTint"s][0].as<float>(params.inner_mid_tint[0]);
            params.inner_mid_tint[1] =
                node["InnerMidTint"s][1].as<float>(params.inner_mid_tint[1]);
            params.inner_mid_tint[2] =
                node["InnerMidTint"s][2].as<float>(params.inner_mid_tint[2]);

            params.mid_scale = node["MidScale"s].as<float>(params.mid_scale);
            params.mid_tint[0] = node["MidTint"s][0].as<float>(params.mid_tint[0]);
            params.mid_tint[1] = node["MidTint"s][1].as<float>(params.mid_tint[1]);
            params.mid_tint[2] = node["MidTint"s][2].as<float>(params.mid_tint[2]);

            params.outer_mid_scale =
                node["OuterMidScale"s].as<float>(params.outer_mid_scale);
            params.outer_mid_tint[0] =
                node["OuterMidTint"s][0].as<float>(params.outer_mid_tint[0]);
            params.outer_mid_tint[1] =
                node["OuterMidTint"s][1].as<float>(params.outer_mid_tint[1]);
            params.outer_mid_tint[2] =
                node["OuterMidTint"s][2].as<float>(params.outer_mid_tint[2]);

            params.outer_scale = node["OuterScale"s].as<float>(params.outer_scale);
            params.outer_tint[0] = node["OuterTint"s][0].as<float>(params.outer_tint[0]);
            params.outer_tint[1] = node["OuterTint"s][1].as<float>(params.outer_tint[1]);
            params.outer_tint[2] = node["OuterTint"s][2].as<float>(params.outer_tint[2]);

            params.use_dirt = node["UseDirt"s].as<bool>(params.use_dirt);

            if (params.mode == sp::effects::Bloom_mode::threshold)
                params.dirt_scale = node["DirtScale"s].as<float>(params.dirt_scale);

            params.dirt_tint[0] = node["DirtTint"s][0].as<float>(params.dirt_tint[0]);
            params.dirt_tint[1] = node["DirtTint"s][1].as<float>(params.dirt_tint[1]);
            params.dirt_tint[2] = node["DirtTint"s][2].as<float>(params.dirt_tint[2]);
            params.dirt_texture_name =
                node["DirtTextureName"s].as<std::string>(params.dirt_texture_name);

            return true;
        }
    };

    template<>
    struct convert<sp::effects::Vignette_params> {
        static Node encode(const sp::effects::Vignette_params& params)
        {
            using namespace std::literals;

            YAML::Node node;

            node["Enable"s] = params.enabled;

            node["End"s] = params.end;
            node["Start"s] = params.start;

            return node;
        }

        static bool decode(const Node& node, sp::effects::Vignette_params& params)
        {
            using namespace std::literals;

            params = sp::effects::Vignette_params{};

            params.enabled = node["Enable"s].as<bool>(params.enabled);

            params.end = node["End"s].as<float>(params.end);
            params.start = node["Start"s].as<float>(params.start);

            return true;
        }
    };

    template<>
    struct convert<sp::effects::Color_grading_params> {
        static Node encode(const sp::effects::Color_grading_params& params)
        {
            using namespace std::literals;

            YAML::Node node;

            node["ColorFilter"s] = params.color_filter;

            node["Saturation"s] = params.saturation;
            node["Exposure"s] = params.exposure;
            node["Brightness"s] = params.brightness;
            node["Contrast"s] = params.contrast;

            node["Tonemapper"s] = params.tonemapper;

            node["FilmicToeStrength"s] = params.filmic_toe_strength;
            node["FilmicToeLength"s] = params.filmic_toe_length;
            node["FilmicShoulderStrength"s] = params.filmic_shoulder_strength;
            node["FilmicShoulderLength"s] = params.filmic_shoulder_length;
            node["FilmicShoulderAngle"s] = params.filmic_shoulder_angle;
            node["FilmicHejiWhitepoint"s] = params.filmic_heji_whitepoint;

            node["ShadowColor"s] = params.shadow_color;
            node["MidtoneColor"s] = params.midtone_color;
            node["HighlightColor"s] = params.highlight_color;

            node["ShadowOffset"s] = params.shadow_offset;
            node["MidtoneOffset"s] = params.midtone_offset;
            node["HighlightOffset"s] = params.highlight_offset;

            node["HSVHueAdjustment"s] = params.hsv_hue_adjustment;
            node["HSVSaturationAdjustment"s] = params.hsv_saturation_adjustment;
            node["HSVValueAdjustment"s] = params.hsv_value_adjustment;

            node["ChannelMixRed"s] = params.channel_mix_red;
            node["ChannelMixGreen"s] = params.channel_mix_green;
            node["ChannelMixBlue"s] = params.channel_mix_blue;

            return node;
        }

        static bool decode(const Node& node, sp::effects::Color_grading_params& params)
        {
            using namespace std::literals;

            params = sp::effects::Color_grading_params{};

            params.color_filter = node["ColorFilter"s].as<glm::vec3>(params.color_filter);

            params.saturation = node["Saturation"s].as<float>(params.saturation);
            params.exposure = node["Exposure"s].as<float>(params.exposure);
            params.brightness = node["Brightness"s].as<float>(params.brightness);
            params.contrast = node["Contrast"s].as<float>(params.contrast);

            params.tonemapper =
                node["Tonemapper"s].as<sp::effects::Tonemapper>(params.tonemapper);

            params.filmic_toe_strength =
                node["FilmicToeStrength"s].as<float>(params.filmic_toe_strength);
            params.filmic_toe_length =
                node["FilmicToeLength"s].as<float>(params.filmic_toe_length);
            params.filmic_shoulder_strength =
                node["FilmicShoulderStrength"s].as<float>(params.filmic_shoulder_strength);
            params.filmic_shoulder_length =
                node["FilmicShoulderLength"s].as<float>(params.filmic_shoulder_length);
            params.filmic_shoulder_angle =
                node["FilmicShoulderAngle"s].as<float>(params.filmic_shoulder_angle);
            params.filmic_heji_whitepoint =
                node["FilmicHejiWhitepoint"s].as<float>(params.filmic_heji_whitepoint);

            params.shadow_color = node["ShadowColor"s].as<glm::vec3>(params.shadow_color);
            params.midtone_color = node["MidtoneColor"s].as<glm::vec3>(params.midtone_color);
            params.highlight_color =
                node["HighlightColor"s].as<glm::vec3>(params.highlight_color);

            params.shadow_offset = node["ShadowOffset"s].as<float>(params.shadow_offset);
            params.midtone_offset = node["MidtoneOffset"s].as<float>(params.midtone_offset);
            params.highlight_offset =
                node["HighlightOffset"s].as<float>(params.highlight_offset);

            params.hsv_hue_adjustment =
                node["HSVHueAdjustment"s].as<float>(params.hsv_hue_adjustment);
            params.hsv_saturation_adjustment =
                node["HSVSaturationAdjustment"s].as<float>(params.hsv_saturation_adjustment);
            params.hsv_value_adjustment =
                node["HSVValueAdjustment"s].as<float>(params.hsv_value_adjustment);

            params.channel_mix_red =
                node["ChannelMixRed"s].as<glm::vec3>(params.channel_mix_red);
            params.channel_mix_green =
                node["ChannelMixGreen"s].as<glm::vec3>(params.channel_mix_green);
            params.channel_mix_blue =
                node["ChannelMixBlue"s].as<glm::vec3>(params.channel_mix_blue);

            return true;
        }
    };

    template<>
    struct convert<sp::effects::Film_grain_params> {
        static Node encode(const sp::effects::Film_grain_params& params)
        {
            using namespace std::literals;

            YAML::Node node;

            node["Enable"s] = params.enabled;
            node["Colored"s] = params.colored;

            node["Amount"s] = params.amount;
            node["Size"s] = params.size;
            node["ColorAmount"s] = params.color_amount;
            node["LumaAmount"s] = params.luma_amount;

            return node;
        }

        static bool decode(const Node& node, sp::effects::Film_grain_params& params)
        {
            using namespace std::literals;

            params = sp::effects::Film_grain_params{};

            params.enabled = node["Enable"s].as<bool>(params.enabled);
            params.colored = node["Colored"s].as<bool>(params.colored);

            params.amount = node["Amount"s].as<float>(params.amount);
            params.size = node["Size"s].as<float>(params.size);
            params.color_amount = node["ColorAmount"s].as<float>(params.color_amount);
            params.luma_amount = node["LumaAmount"s].as<float>(params.luma_amount);

            return true;
        }
    };

    template<>
    struct convert<sp::effects::SSAO_params> {
        static Node encode(const sp::effects::SSAO_params& params)
        {
            using namespace std::literals;

            YAML::Node node;

            node["Enable"s] = params.enabled;
            node["Mode"s] = params.mode;
            node["Method"s] = params.method;
            node["Radius"s] = params.radius;
            node["Shadow Multiplier"s] = params.shadow_multiplier;
            node["Shadow Power"s] = params.shadow_power;
            node["Detail Shadow Strength"s] = params.detail_shadow_strength;
            node["Blur Amount"s] = params.blur_pass_count;
            node["sharpness"s] = params.sharpness;

            return node;
        }

        static bool decode(const Node& node, sp::effects::SSAO_params& params)
        {
            using namespace std::literals;

            params = sp::effects::SSAO_params{};

            params.enabled = node["Enable"s].as<bool>(false);
            params.mode =
                node["Mode"s].as<sp::effects::SSAO_mode>(sp::effects::SSAO_mode::global);
            params.method =
                node["Method"s].as<sp::effects::SSAO_method>(sp::effects::SSAO_method::assao);
            params.radius = node["Radius"s].as<float>(params.radius);
            params.shadow_multiplier =
                node["Shadow Multiplier"s].as<float>(params.shadow_multiplier);
            params.shadow_power = node["Shadow Power"s].as<float>(params.shadow_power);
            params.detail_shadow_strength =
                node["Detail Shadow Strength"s].as<float>(params.detail_shadow_strength);
            params.blur_pass_count = node["Blur Amount"s].as<int>(params.blur_pass_count);
            params.sharpness = node["sharpness"s].as<float>(params.sharpness);

            return true;
        }
    };

    template<>
    struct convert<sp::effects::FFX_cas_params> {
        static Node encode(const sp::effects::FFX_cas_params& params)
        {
            using namespace std::literals;

            YAML::Node node;

            node["Enable"s] = params.enabled;
            node["Sharpness"s] = params.sharpness;

            return node;
        }

        static bool decode(const Node& node, sp::effects::FFX_cas_params& params)
        {
            using namespace std::literals;

            params = sp::effects::FFX_cas_params{};

            params.enabled = node["Enable"s].as<bool>(params.enabled);
            params.sharpness = node["Sharpness"s].as<float>(params.sharpness);

            return true;
        }
    };

    template<>
    struct convert<sp::effects::DOF_params> {
        static Node encode(const sp::effects::DOF_params& params)
        {
            using namespace std::literals;

            YAML::Node node;

            node["Enable"s] = params.enabled;
            node["FilmSize"s] = params.film_size_mm;
            node["FocusDistance"s] = params.focus_distance;
            node["FStop"s] = params.f_stop;

            return node;
        }

        static bool decode(const Node& node, sp::effects::DOF_params& params)
        {
            using namespace std::literals;

            params = sp::effects::DOF_params{};

            params.enabled = node["Enable"s].as<bool>(params.enabled);
            params.film_size_mm = node["FilmSize"s].as<float>(params.film_size_mm);
            params.focus_distance = node["FocusDistance"s].as<float>(params.focus_distance);
            params.f_stop = node["FStop"s].as<float>(params.f_stop);

            params.film_size_mm = std::max(params.film_size_mm, 1.0f);
            params.focus_distance = std::max(params.focus_distance, 0.0f);
            params.f_stop = std::max(params.f_stop, 1.0f);

            return true;
        }
    };

    template<>
    struct convert<sp::effects::Fog_params> {
        static Node encode(const sp::effects::Fog_params& params)
        {
            using namespace std::literals;

            YAML::Node node;

            node["Enable"s] = params.enabled;
            node["Color"s] = params.color;
            node["Density"s] = params.density;
            node["StartDistance"s] = params.start_distance;
            node["EndDistance"s] = params.end_distance;

            node["HeightFogEnable"s] = params.height_fog_enabled;
            node["HeightDensity"s] = params.height_density;
            node["HeightBase"s] = params.height_base;
            node["HeightFalloff"s] = params.height_falloff;

            node["SunInscatterEnable"s] = params.sun_inscatter_enabled;
            node["SunDirection"s] = params.sun_direction;
            node["SunColor"s] = params.sun_color;
            node["SunIntensity"s] = params.sun_intensity;
            node["SunPower"s] = params.sun_power;

            node["MaxOpacity"s] = params.max_opacity;

            node["NoiseEnable"s] = params.noise_enabled;
            node["NoiseScale"s] = params.noise_scale;
            node["NoiseIntensity"s] = params.noise_intensity;
            node["NoiseSpeed"s] = params.noise_speed;

            node["AtmosphereEnable"s] = params.atmosphere_enabled;
            node["AtmosIntensity"s] = params.atmos_intensity;
            node["AtmosFalloff"s] = params.atmos_falloff;
            node["HorizonOffset"s] = params.horizon_offset;
            node["AtmosphereTexture"s] = params.atmosphere_texture_name;

            // BF3 height blending
            node["AtmosHighIntensity"s] = params.atmos_high_intensity;
            node["HeightBlendWeight"s] = params.height_blend_weight;

            node["ZenithHaze"s] = params.zenith_haze;

            // Space cubemap blending
            node["SpaceTexture"s] = params.space_texture_name;
            node["AltitudeBlendStart"s] = params.altitude_blend_start;
            node["AltitudeBlendEnd"s] = params.altitude_blend_end;
            node["SkyBlendOverride"s] = params.sky_blend_override;

            // Cubemap alignment transform
            node["CubemapRotation"s] = params.cubemap_rotation;
            node["CubemapScale"s] = params.cubemap_scale;
            node["CubemapOffset"s] = params.cubemap_offset;
            node["CubemapMipScale"s] = params.cubemap_mip_scale;

            return node;
        }

        static bool decode(const Node& node, sp::effects::Fog_params& params)
        {
            using namespace std::literals;

            params = sp::effects::Fog_params{};

            params.enabled = node["Enable"s].as<bool>(params.enabled);
            params.color = node["Color"s].as<glm::vec3>(params.color);
            params.density = node["Density"s].as<float>(params.density);
            params.start_distance = node["StartDistance"s].as<float>(params.start_distance);
            params.end_distance = node["EndDistance"s].as<float>(params.end_distance);

            params.height_fog_enabled = node["HeightFogEnable"s].as<bool>(params.height_fog_enabled);
            params.height_density = node["HeightDensity"s].as<float>(params.height_density);
            params.height_base = node["HeightBase"s].as<float>(params.height_base);
            params.height_falloff = node["HeightFalloff"s].as<float>(params.height_falloff);

            params.sun_inscatter_enabled = node["SunInscatterEnable"s].as<bool>(params.sun_inscatter_enabled);
            params.sun_direction = node["SunDirection"s].as<glm::vec3>(params.sun_direction);
            params.sun_color = node["SunColor"s].as<glm::vec3>(params.sun_color);
            params.sun_intensity = node["SunIntensity"s].as<float>(params.sun_intensity);
            params.sun_power = node["SunPower"s].as<float>(params.sun_power);

            params.max_opacity = node["MaxOpacity"s].as<float>(params.max_opacity);

            params.noise_enabled = node["NoiseEnable"s].as<bool>(params.noise_enabled);
            params.noise_scale = node["NoiseScale"s].as<float>(params.noise_scale);
            params.noise_intensity = node["NoiseIntensity"s].as<float>(params.noise_intensity);
            params.noise_speed = node["NoiseSpeed"s].as<float>(params.noise_speed);

            params.atmosphere_enabled = node["AtmosphereEnable"s].as<bool>(params.atmosphere_enabled);
            // Support both old "AtmosDensity" and new "AtmosIntensity" for backwards compatibility
            params.atmos_intensity = node["AtmosIntensity"s].as<float>(
                node["AtmosDensity"s].as<float>(params.atmos_intensity));
            params.atmos_falloff = node["AtmosFalloff"s].as<float>(params.atmos_falloff);
            params.horizon_offset = node["HorizonOffset"s].as<float>(params.horizon_offset);
            params.atmosphere_texture_name = node["AtmosphereTexture"s].as<std::string>(params.atmosphere_texture_name);

            // BF3 height blending
            params.atmos_high_intensity = node["AtmosHighIntensity"s].as<float>(params.atmos_high_intensity);
            params.height_blend_weight = node["HeightBlendWeight"s].as<float>(params.height_blend_weight);

            params.zenith_haze = node["ZenithHaze"s].as<float>(params.zenith_haze);

            // Space cubemap blending
            params.space_texture_name = node["SpaceTexture"s].as<std::string>(params.space_texture_name);
            params.altitude_blend_start = node["AltitudeBlendStart"s].as<float>(params.altitude_blend_start);
            params.altitude_blend_end = node["AltitudeBlendEnd"s].as<float>(params.altitude_blend_end);
            params.sky_blend_override = node["SkyBlendOverride"s].as<float>(params.sky_blend_override);

            // Cubemap alignment transform
            params.cubemap_rotation = node["CubemapRotation"s].as<glm::vec3>(params.cubemap_rotation);
            params.cubemap_scale = node["CubemapScale"s].as<glm::vec3>(params.cubemap_scale);
            params.cubemap_offset = node["CubemapOffset"s].as<glm::vec3>(params.cubemap_offset);
            params.cubemap_mip_scale = node["CubemapMipScale"s].as<float>(params.cubemap_mip_scale);

            return true;
        }
    };

    template<>
    struct convert<sp::effects::Sky_dome_params> {
        static Node encode(const sp::effects::Sky_dome_params& params)
        {
            using namespace std::literals;

            YAML::Node node;

            node["Enable"s] = params.enabled;
            node["AtmosphereTexture"s] = params.atmosphere_texture_name;
            node["AtmosphereDensity"s] = params.atmosphere_density;

            node["HorizonShift"s] = params.horizon_shift;
            node["HorizonStart"s] = params.horizon_start;
            node["HorizonBlend"s] = params.horizon_blend;

            node["FadeStartHeight"s] = params.fade_start_height;
            node["FadeEndHeight"s] = params.fade_end_height;

            node["Tint"s] = params.tint;

            node["CubemapRotation"s] = params.cubemap_rotation;
            node["CubemapScale"s] = params.cubemap_scale;
            node["CubemapOffset"s] = params.cubemap_offset;

            return node;
        }

        static bool decode(const Node& node, sp::effects::Sky_dome_params& params)
        {
            using namespace std::literals;

            params = sp::effects::Sky_dome_params{};

            params.enabled = node["Enable"s].as<bool>(params.enabled);
            params.atmosphere_texture_name = node["AtmosphereTexture"s].as<std::string>(params.atmosphere_texture_name);
            params.atmosphere_density = node["AtmosphereDensity"s].as<float>(params.atmosphere_density);

            params.horizon_shift = node["HorizonShift"s].as<float>(params.horizon_shift);
            params.horizon_start = node["HorizonStart"s].as<float>(params.horizon_start);
            params.horizon_blend = node["HorizonBlend"s].as<float>(params.horizon_blend);

            params.fade_start_height = node["FadeStartHeight"s].as<float>(params.fade_start_height);
            params.fade_end_height = node["FadeEndHeight"s].as<float>(params.fade_end_height);

            params.tint = node["Tint"s].as<glm::vec3>(params.tint);

            params.cubemap_rotation = node["CubemapRotation"s].as<glm::vec3>(params.cubemap_rotation);
            params.cubemap_scale = node["CubemapScale"s].as<glm::vec3>(params.cubemap_scale);
            params.cubemap_offset = node["CubemapOffset"s].as<glm::vec3>(params.cubemap_offset);

            return true;
        }
    };

    template<>
    struct convert<sp::effects::Sky_bf3_params> {
        static Node encode(const sp::effects::Sky_bf3_params& params)
        {
            using namespace std::literals;

            YAML::Node node;

            node["Enable"s] = params.enabled;
            node["Mode"s] = sp::effects::to_string(params.mode);

            node["AtmosDensity"s] = params.atmos_density;
            node["HorizonShift"s] = params.horizon_shift;
            node["HorizonStart"s] = params.horizon_start;
            node["HorizonBlend"s] = params.horizon_blend;

            node["BlendStartHeight"s] = params.blend_start_height;
            node["BlendEndHeight"s] = params.blend_end_height;
            node["ManualBlend"s] = params.manual_blend;

            node["GroundCubemap"s] = params.ground_cubemap;
            node["SpaceCubemap"s] = params.space_cubemap;
            node["AtmosphereCubemap"s] = params.atmosphere_cubemap;

            node["CubemapRotation"s] = params.cubemap_rotation;
            node["CubemapScale"s] = params.cubemap_scale;
            node["CubemapOffset"s] = params.cubemap_offset;

            node["Monochrome"s] = params.monochrome;
            node["UseRGBE"s] = params.use_rgbe;
            node["AmbientColor"s] = params.ambient_color;
            node["Tint"s] = params.tint;

            return node;
        }

        static bool decode(const Node& node, sp::effects::Sky_bf3_params& params)
        {
            using namespace std::literals;

            params = sp::effects::Sky_bf3_params{};

            params.enabled = node["Enable"s].as<bool>(params.enabled);
            params.mode = sp::effects::sky_bf3_mode_from_string(
                node["Mode"s].as<std::string>(sp::effects::to_string(params.mode)));

            params.atmos_density = node["AtmosDensity"s].as<float>(params.atmos_density);
            params.horizon_shift = node["HorizonShift"s].as<float>(params.horizon_shift);
            params.horizon_start = node["HorizonStart"s].as<float>(params.horizon_start);
            params.horizon_blend = node["HorizonBlend"s].as<float>(params.horizon_blend);

            params.blend_start_height = node["BlendStartHeight"s].as<float>(params.blend_start_height);
            params.blend_end_height = node["BlendEndHeight"s].as<float>(params.blend_end_height);
            params.manual_blend = node["ManualBlend"s].as<float>(params.manual_blend);

            params.ground_cubemap = node["GroundCubemap"s].as<std::string>(params.ground_cubemap);
            params.space_cubemap = node["SpaceCubemap"s].as<std::string>(params.space_cubemap);
            params.atmosphere_cubemap = node["AtmosphereCubemap"s].as<std::string>(params.atmosphere_cubemap);

            params.cubemap_rotation = node["CubemapRotation"s].as<glm::vec3>(params.cubemap_rotation);
            params.cubemap_scale = node["CubemapScale"s].as<glm::vec3>(params.cubemap_scale);
            params.cubemap_offset = node["CubemapOffset"s].as<glm::vec3>(params.cubemap_offset);

            params.monochrome = node["Monochrome"s].as<bool>(params.monochrome);
            params.use_rgbe = node["UseRGBE"s].as<bool>(params.use_rgbe);
            params.ambient_color = node["AmbientColor"s].as<glm::vec3>(params.ambient_color);
            params.tint = node["Tint"s].as<glm::vec3>(params.tint);

            return true;
        }
    };

    template<>
    struct convert<sp::effects::Cubemap_alignment> {
        static Node encode(const sp::effects::Cubemap_alignment& params)
        {
            using namespace std::literals;

            YAML::Node node;

            node["Rotation"s] = params.rotation;
            node["Scale"s] = params.scale;
            node["Offset"s] = params.offset;
            node["MipScale"s] = params.mip_scale;

            return node;
        }

        static bool decode(const Node& node, sp::effects::Cubemap_alignment& params)
        {
            using namespace std::literals;

            params = sp::effects::Cubemap_alignment{};

            params.rotation = node["Rotation"s].as<glm::vec3>(params.rotation);
            params.scale = node["Scale"s].as<glm::vec3>(params.scale);
            params.offset = node["Offset"s].as<glm::vec3>(params.offset);
            params.mip_scale = node["MipScale"s].as<float>(params.mip_scale);

            return true;
        }
    };

    template<>
    struct convert<sp::effects::Skybox_override_params> {
        static Node encode(const sp::effects::Skybox_override_params& params)
        {
            using namespace std::literals;

            YAML::Node node;

            node["Enable"s] = params.enabled;
            node["GroundCubemap"s] = params.ground_cubemap;
            node["SkyCubemap"s] = params.sky_cubemap;
            node["SkyDistanceThreshold"s] = params.sky_distance_threshold;
            node["AtmosDensity"s] = params.atmos_density;
            node["HorizonShift"s] = params.horizon_shift;
            node["HorizonStart"s] = params.horizon_start;
            node["HorizonBlend"s] = params.horizon_blend;
            node["Tint"s] = params.tint;

            return node;
        }

        static bool decode(const Node& node, sp::effects::Skybox_override_params& params)
        {
            using namespace std::literals;

            params = sp::effects::Skybox_override_params{};

            params.enabled = node["Enable"s].as<bool>(params.enabled);
            params.ground_cubemap = node["GroundCubemap"s].as<std::string>(params.ground_cubemap);
            params.sky_cubemap = node["SkyCubemap"s].as<std::string>(params.sky_cubemap);
            params.sky_distance_threshold = node["SkyDistanceThreshold"s].as<float>(params.sky_distance_threshold);
            params.atmos_density = node["AtmosDensity"s].as<float>(params.atmos_density);
            params.horizon_shift = node["HorizonShift"s].as<float>(params.horizon_shift);
            params.horizon_start = node["HorizonStart"s].as<float>(params.horizon_start);
            params.horizon_blend = node["HorizonBlend"s].as<float>(params.horizon_blend);
            params.tint = node["Tint"s].as<glm::vec3>(params.tint);

            return true;
        }
    };

}
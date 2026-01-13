// ===========================================================================
// CLOUD PARAMS v13 - Billboard particles
// ===========================================================================

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <random>
#include <yaml-cpp/yaml.h>
#include "glm_yaml_adapters.hpp"

namespace sp::effects {

    struct Cloud_volume {
        glm::vec3 position = { 0.0f, 300.0f, 0.0f };
        float radius = 100.0f;
        glm::vec3 scale = { 1.0f, 0.3f, 1.0f };
        float density = 1.0f;
    };

    struct Cloud_params {
        bool enabled = false;

        // Particle settings
        int particles_per_volume = 30;
        float particle_size = 80.0f;
        float cloud_alpha = 0.6f;

        // Appearance
        glm::vec3 light_color = { 1.0f, 0.98f, 0.95f };
        glm::vec3 dark_color = { 0.7f, 0.75f, 0.85f };

        // Animation
        float wind_speed = 2.0f;
        glm::vec2 wind_direction = { 1.0f, 0.0f };

        // Fade
        float fade_near = 100.0f;
        float fade_far = 5000.0f;
        float depth_cutoff_distance = 50.0f;

        std::vector<Cloud_volume> volumes;

        void generate_default_volumes(int count, float height, float spread)
        {
            volumes.clear();
            volumes.reserve(count);

            std::mt19937 rng(42);
            std::uniform_real_distribution<float> dist_01(0.0f, 1.0f);
            std::uniform_real_distribution<float> dist_angle(0.0f, 6.28318f);

            for (int i = 0; i < count; ++i) {
                Cloud_volume vol;

                // Random position in disk
                float angle = dist_angle(rng);
                float r = std::sqrt(dist_01(rng)) * spread;

                vol.position.x = std::cos(angle) * r;
                vol.position.z = std::sin(angle) * r;
                vol.position.y = height + (dist_01(rng) - 0.5f) * spread * 0.05f;

                // Size
                vol.radius = 60.0f + dist_01(rng) * 120.0f;

                // Flat ellipsoids
                vol.scale.x = 0.8f + dist_01(rng) * 0.5f;
                vol.scale.y = 0.2f + dist_01(rng) * 0.2f;
                vol.scale.z = 0.8f + dist_01(rng) * 0.5f;

                vol.density = 0.7f + dist_01(rng) * 0.5f;

                volumes.push_back(vol);
            }
        }
    };

    inline auto to_string([[maybe_unused]] const Cloud_params& params) noexcept
    {
        return std::string{ "Cloud_params" };
    }

} // namespace sp::effects

namespace YAML {

    template<>
    struct convert<sp::effects::Cloud_volume> {
        static Node encode(const sp::effects::Cloud_volume& vol)
        {
            using namespace std::literals;
            Node node;
            node["Position"s] = vol.position;
            node["Radius"s] = vol.radius;
            node["Scale"s] = vol.scale;
            node["Density"s] = vol.density;
            return node;
        }

        static bool decode(const Node& node, sp::effects::Cloud_volume& vol)
        {
            using namespace std::literals;
            vol = sp::effects::Cloud_volume{};
            vol.position = node["Position"s].as<glm::vec3>(vol.position);
            vol.radius = node["Radius"s].as<float>(vol.radius);
            vol.scale = node["Scale"s].as<glm::vec3>(vol.scale);
            vol.density = node["Density"s].as<float>(vol.density);
            return true;
        }
    };

    template<>
    struct convert<sp::effects::Cloud_params> {
        static Node encode(const sp::effects::Cloud_params& params)
        {
            using namespace std::literals;
            Node node;
            node["Enable"s] = params.enabled;
            node["ParticlesPerVolume"s] = params.particles_per_volume;
            node["ParticleSize"s] = params.particle_size;
            node["CloudAlpha"s] = params.cloud_alpha;
            node["LightColor"s] = params.light_color;
            node["DarkColor"s] = params.dark_color;
            node["WindSpeed"s] = params.wind_speed;
            node["WindDirection"s] = params.wind_direction;
            node["FadeNear"s] = params.fade_near;
            node["FadeFar"s] = params.fade_far;
            node["DepthCutoffDistance"s] = params.depth_cutoff_distance;

            Node volumes_node;
            for (const auto& vol : params.volumes)
                volumes_node.push_back(vol);
            node["Volumes"s] = volumes_node;
            return node;
        }

        static bool decode(const Node& node, sp::effects::Cloud_params& params)
        {
            using namespace std::literals;
            params = sp::effects::Cloud_params{};
            params.enabled = node["Enable"s].as<bool>(params.enabled);
            params.particles_per_volume = node["ParticlesPerVolume"s].as<int>(params.particles_per_volume);
            params.particle_size = node["ParticleSize"s].as<float>(params.particle_size);
            params.cloud_alpha = node["CloudAlpha"s].as<float>(params.cloud_alpha);
            params.light_color = node["LightColor"s].as<glm::vec3>(params.light_color);
            params.dark_color = node["DarkColor"s].as<glm::vec3>(params.dark_color);
            params.wind_speed = node["WindSpeed"s].as<float>(params.wind_speed);
            params.wind_direction = node["WindDirection"s].as<glm::vec2>(params.wind_direction);
            params.fade_near = node["FadeNear"s].as<float>(params.fade_near);
            params.fade_far = node["FadeFar"s].as<float>(params.fade_far);
            params.depth_cutoff_distance = node["DepthCutoffDistance"s].as<float>(params.depth_cutoff_distance);

            params.volumes.clear();
            if (node["Volumes"s]) {
                for (const auto& vol_node : node["Volumes"s])
                    params.volumes.push_back(vol_node.as<sp::effects::Cloud_volume>());
            }
            return true;
        }
    };

} // namespace YAML

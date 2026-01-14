// ===========================================================================
// CLOUDS_BF3_PARAMS.HPP - BF3-Style Cloud Parameters
// ===========================================================================

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <random>

namespace sp::effects {

    // A cloud volume defines where particles are spawned
    struct Cloud_volume_bf3 {
        glm::vec3 position = { 0.0f, 500.0f, 0.0f };
        glm::vec3 scale = { 200.0f, 50.0f, 200.0f };  // Ellipsoid radii
        float density = 1.0f;
        int cluster_count = 8;      // Number of particle clusters in this volume
        int particles_per_cluster = 6;  // Particles per cluster
    };

    struct Cloud_params_bf3 {
        bool enabled = true;

        // ----- Particle Generation (larger, more dramatic) -----
        float particle_size_min = 100.0f;
        float particle_size_max = 280.0f;
        float cluster_radius = 70.0f;
        
        // ----- Cloud Colors (dramatic contrast) -----
        glm::vec3 color_bright = { 1.0f, 0.98f, 0.94f };   // Warm sun-lit
        glm::vec3 color_dark = { 0.45f, 0.5f, 0.6f };      // Cool shadows
        glm::vec3 color_ambient = { 0.65f, 0.72f, 0.85f }; // Sky blue ambient
        
        // ----- Noise Settings (soft edges, no hard cutoffs) -----
        glm::vec4 octave_weights = { 0.5f, 0.35f, 0.2f, 0.15f };
        float noise_scale = 0.004f;
        float noise_erosion = 0.6f;         // Moderate erosion
        float noise_edge_sharpness = 0.8f;  // < 1.0 = softer, > 1.0 = sharper
        float noise_animation_speed = 0.006f;
        
        // ----- Spectacular Lighting -----
        float sun_intensity = 1.5f;
        float ambient_intensity = 0.3f;
        float scatter_forward = 0.85f;      // Strong silver lining
        float scatter_forward_exp = 5.0f;
        float scatter_back = 0.25f;
        float absorption = 0.6f;
        float powder_strength = 0.4f;
        float ambient_boost = 0.7f;
        float self_shadow = 0.5f;           // Strong self-shadowing
        
        // ----- Distance Fade -----
        float fade_near = 80.0f;
        float fade_far = 10000.0f;
        float global_alpha = 0.95f;
        
        // ----- Soft Particles -----
        float depth_softness = 0.0008f;
        
        // ----- Low-Res Rendering -----
        float resolution_scale = 0.25f;
        float upsample_sharpness = 0.1f;
        
        // ----- Wind -----
        glm::vec2 wind_direction = { 1.0f, 0.3f };
        float wind_speed = 8.0f;

        // ----- Volumes -----
        std::vector<Cloud_volume_bf3> volumes;

        // Constructor - generate spectacular cloud field
        Cloud_params_bf3() {
            generate_cloud_field(50, 450.0f, 4000.0f);
        }

        // Generate a typical cloud field
        void generate_cloud_field(int volume_count, float base_height, float spread_radius)
        {
            volumes.clear();
            volumes.reserve(volume_count);

            std::mt19937 rng(42);  // Fixed seed for reproducibility
            std::uniform_real_distribution<float> dist_01(0.0f, 1.0f);
            std::uniform_real_distribution<float> dist_angle(0.0f, 6.28318f);

            for (int i = 0; i < volume_count; ++i) {
                Cloud_volume_bf3 vol;

                // Distribute in a disk pattern with some clustering
                float angle = dist_angle(rng);
                float r = std::sqrt(dist_01(rng)) * spread_radius;
                
                // Add some clustering - clouds tend to group together
                if (dist_01(rng) > 0.7f && !volumes.empty()) {
                    // Cluster near an existing cloud
                    int cluster_idx = static_cast<int>(dist_01(rng) * volumes.size());
                    vol.position.x = volumes[cluster_idx].position.x + (dist_01(rng) - 0.5f) * 400.0f;
                    vol.position.z = volumes[cluster_idx].position.z + (dist_01(rng) - 0.5f) * 400.0f;
                } else {
                    vol.position.x = std::cos(angle) * r;
                    vol.position.z = std::sin(angle) * r;
                }
                
                // Height variation - cumulus clouds have flat bases, puffy tops
                float height_variation = dist_01(rng);
                vol.position.y = base_height + height_variation * height_variation * 150.0f; // More at base

                // Varied ellipsoid sizes - some big dramatic ones
                float size_mult = 0.4f + dist_01(rng) * dist_01(rng) * 1.8f; // Bias towards medium
                vol.scale.x = 180.0f * size_mult + dist_01(rng) * 150.0f;
                vol.scale.y = 40.0f * size_mult + dist_01(rng) * 50.0f;  // Taller for dramatic look
                vol.scale.z = 180.0f * size_mult + dist_01(rng) * 150.0f;

                vol.density = 0.5f + dist_01(rng) * 0.7f;
                
                // More particles for larger, denser clouds
                vol.cluster_count = 5 + static_cast<int>(size_mult * 8);
                vol.particles_per_cluster = 5 + static_cast<int>(dist_01(rng) * 6);

                volumes.push_back(vol);
            }
        }
    };

} // namespace sp::effects

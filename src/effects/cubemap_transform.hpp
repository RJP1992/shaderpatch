#pragma once

#include "postprocess_params.hpp"

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace sp::effects {

    // Packed cubemap transform for HLSL constant buffer
    // HLSL float3x3 is stored as 3 rows of float4 (with padding)
    struct Cubemap_transform_packed {
        glm::vec4 rotation_row0 = {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 rotation_row1 = {0.0f, 1.0f, 0.0f, 0.0f};
        glm::vec4 rotation_row2 = {0.0f, 0.0f, 1.0f, 0.0f};
        glm::vec3 scale = {1.0f, 1.0f, 1.0f};
        float _pad0 = 0.0f;
        glm::vec3 offset = {0.0f, 0.0f, 0.0f};
        float _pad1 = 0.0f;
    };

    // Build rotation matrix from Euler angles (degrees)
    inline glm::mat3 build_cubemap_rotation(const glm::vec3& euler_degrees) noexcept
    {
        const glm::vec3 rot_rad = glm::radians(euler_degrees);
        return glm::mat3(glm::yawPitchRoll(rot_rad.y, rot_rad.x, rot_rad.z));
    }

    // Pack Cubemap_alignment into cbuffer-friendly format
    inline Cubemap_transform_packed pack_cubemap_transform(const Cubemap_alignment& alignment) noexcept
    {
        Cubemap_transform_packed packed;

        const glm::mat3 rotation = build_cubemap_rotation(alignment.rotation);

        // Store as 3 rows (HLSL float3x3 layout with padding)
        packed.rotation_row0 = glm::vec4(rotation[0], 0.0f);
        packed.rotation_row1 = glm::vec4(rotation[1], 0.0f);
        packed.rotation_row2 = glm::vec4(rotation[2], 0.0f);

        packed.scale = alignment.scale;
        packed.offset = alignment.offset;

        return packed;
    }

}

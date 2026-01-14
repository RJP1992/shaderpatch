// ===========================================================================
// CLOUDS_BF3.HPP - BF3-Style Cloud Rendering
// ===========================================================================

#pragma once

#include "clouds_bf3_params.hpp"
#include "rendertarget_allocator.hpp"
#include "com_ptr.hpp"
#include "profiler.hpp"
#include "../shader/database.hpp"
#include "../core/texture_database.hpp"

#include <d3d11_4.h>
#include <glm/glm.hpp>
#include <memory>
#include <array>

namespace sp::effects {

    struct Clouds_bf3_input {
        UINT width;
        UINT height;
        
        glm::mat4 view_matrix;
        glm::mat4 proj_matrix;
        glm::vec3 camera_position;
        
        glm::vec3 sun_direction;    // Direction TO the sun (normalized)
        glm::vec3 sun_color;
        
        float time;
        
        // First-person depth buffer for occlusion (may be nullptr)
        ID3D11ShaderResourceView* firstperson_depth_srv;
    };

    class Clouds_bf3 {
    public:
        Clouds_bf3(Com_ptr<ID3D11Device5> device, shader::Database& shaders);
        ~Clouds_bf3();

        Clouds_bf3(const Clouds_bf3&) = delete;
        Clouds_bf3& operator=(const Clouds_bf3&) = delete;
        Clouds_bf3(Clouds_bf3&&) = default;
        Clouds_bf3& operator=(Clouds_bf3&&) = default;

        void set_params(const Cloud_params_bf3& params) noexcept;
        const Cloud_params_bf3& get_params() const noexcept;

        // Render clouds to the scene
        void render(ID3D11DeviceContext1& dc,
                    Rendertarget_allocator& rt_allocator,
                    ID3D11RenderTargetView& output_rtv,
                    ID3D11DepthStencilView& depth_dsv,
                    ID3D11ShaderResourceView& depth_srv,
                    const core::Shader_resource_database& textures,
                    const Clouds_bf3_input& input,
                    std::array<ID3D11UnorderedAccessView*, 3> oit_uavs,
                    Profiler& profiler) noexcept;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace sp::effects

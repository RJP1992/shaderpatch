// ===========================================================================
// CLOUDS.HPP - Volumetric Cloud System
// ===========================================================================

#pragma once

#include "../shader/database.hpp"
#include "clouds_params.hpp"
#include "com_ptr.hpp"
#include "profiler.hpp"

#include <d3d11_4.h>

namespace sp::effects {

    struct Clouds_input {
        ID3D11ShaderResourceView& depth_srv;

        UINT width;
        UINT height;

        glm::mat4 view_matrix;
        glm::mat4 projection_matrix;
        glm::vec3 camera_position;
        glm::vec3 sun_direction;
        float time;
    };

    class Clouds {
    public:
        Clouds(Com_ptr<ID3D11Device5> device, shader::Database& shaders);
        ~Clouds();

        void params(const Cloud_params& params) noexcept;
        auto params() const noexcept -> const Cloud_params&;

        // Render clouds to the scene
        // Call after main scene render, before postprocessing
        void render(ID3D11DeviceContext1& dc,
            ID3D11RenderTargetView& output_rtv,
            const Clouds_input& input,
            Profiler& profiler) noexcept;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace sp::effects

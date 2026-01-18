#pragma once

#include "../shader/database.hpp"
#include "com_ptr.hpp"
#include "cubemap_transform.hpp"
#include "postprocess_params.hpp"
#include "profiler.hpp"

#include <d3d11_4.h>
#include <glm/glm.hpp>

namespace sp::effects {

// Input for post-process skybox rendering
struct Skybox_override_input {
   glm::mat4 view_matrix;
   glm::mat4 projection_matrix;
   glm::vec3 camera_position;
   UINT width;
   UINT height;
   ID3D11ShaderResourceView* ground_cubemap_srv;  // Main sky cubemap (ground level view)
   ID3D11ShaderResourceView* sky_cubemap_srv;     // Atmosphere/space for blending (optional)
   ID3D11ShaderResourceView* depth_near;
   ID3D11ShaderResourceView* depth_far;
   ID3D11ShaderResourceView* stencil_near;        // Stencil buffer for sky detection
   ID3D11ShaderResourceView* stencil_far;
};

class Skybox_override {
public:
   Skybox_override(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept;

   // Post-process render (with depth check)
   void render(ID3D11DeviceContext1& dc,
               Profiler& profiler,
               ID3D11RenderTargetView& output_rtv,
               const Cubemap_alignment& cubemap_alignment,
               const Skybox_override_input& input) noexcept;

   auto params() const noexcept -> const Skybox_override_params& { return _params; }
   void params(const Skybox_override_params& params) noexcept { _params = params; }

private:
   // GPU constant buffer structure (must match HLSL postprocess_skybox.fx)
   struct alignas(16) Constants {
      glm::mat4 inv_view_proj;           // 64 bytes

      glm::vec3 camera_position;         // 12 bytes
      float sky_distance_threshold;      // 4 bytes

      // Cubemap alignment transform (HLSL float3x3 as 3 rows of float4)
      glm::vec4 cubemap_rotation_row0;   // 16 bytes
      glm::vec4 cubemap_rotation_row1;   // 16 bytes
      glm::vec4 cubemap_rotation_row2;   // 16 bytes
      glm::vec3 cubemap_scale;           // 12 bytes
      float proj_from_view_m33;          // 4 bytes - for DOF-style depth conversion
      glm::vec3 cubemap_offset;          // 12 bytes
      float proj_from_view_m43;          // 4 bytes - for DOF-style depth conversion

      // Atmosphere params
      float atmos_density;               // 4 bytes
      float horizon_shift;               // 4 bytes
      float horizon_start;               // 4 bytes
      float horizon_blend;               // 4 bytes

      glm::vec3 tint;                    // 12 bytes
      float use_atmosphere;              // 4 bytes

      float debug_mode;                  // 4 bytes
      glm::vec3 _pad;                    // 12 bytes
   };
   static_assert(sizeof(Constants) == 208);

   [[nodiscard]] auto pack_constants(const Cubemap_alignment& cubemap_alignment,
                                     const Skybox_override_input& input,
                                     bool has_sky_cubemap) const noexcept -> Constants;

   Skybox_override_params _params;

   Com_ptr<ID3D11Device5> _device;
   Com_ptr<ID3D11Buffer> _constant_buffer;
   Com_ptr<ID3D11VertexShader> _vs;
   Com_ptr<ID3D11PixelShader> _ps;
   Com_ptr<ID3D11BlendState> _opaque_blend_state;
   Com_ptr<ID3D11DepthStencilState> _no_depth_state;
   Com_ptr<ID3D11SamplerState> _linear_clamp_sampler;
};

}

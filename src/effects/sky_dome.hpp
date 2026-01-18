#pragma once

#include "../shader/database.hpp"
#include "com_ptr.hpp"
#include "postprocess_params.hpp"
#include "profiler.hpp"

#include <d3d11_4.h>
#include <glm/glm.hpp>

namespace sp::effects {

struct Sky_dome_input {
   glm::mat4 view_matrix;
   glm::mat4 projection_matrix;
   glm::vec3 camera_position;
   UINT width;
   UINT height;
   ID3D11ShaderResourceView* atmosphere_cubemap_srv;
   ID3D11ShaderResourceView* depth_near;
   ID3D11ShaderResourceView* depth_far;
};

class Sky_dome {
public:
   Sky_dome(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept;

   void render(ID3D11DeviceContext1& dc,
               Profiler& profiler,
               ID3D11RenderTargetView& output_rtv,
               const Sky_dome_input& input) noexcept;

   auto params() const noexcept -> const Sky_dome_params& { return _params; }
   void params(const Sky_dome_params& params) noexcept { _params = params; }

private:
   // GPU constant buffer structure (must match HLSL sky_dome.fx)
   struct alignas(16) Constants {
      glm::mat4 inv_view_proj;           // 64 bytes

      glm::vec3 camera_position;         // 12 bytes
      float atmosphere_density;          // 4 bytes

      float horizon_shift;               // 4 bytes
      float horizon_start;               // 4 bytes
      float horizon_blend;               // 4 bytes
      float fade_start_height;           // 4 bytes

      float fade_end_height;             // 4 bytes
      glm::vec3 tint;                    // 12 bytes

      // Cubemap alignment transform (HLSL float3x3 is stored as 3 rows of float4)
      glm::vec4 cubemap_rotation_row0;   // 16 bytes
      glm::vec4 cubemap_rotation_row1;   // 16 bytes
      glm::vec4 cubemap_rotation_row2;   // 16 bytes
      glm::vec3 cubemap_scale;           // 12 bytes
      float _pad_scale;                  // 4 bytes
      glm::vec3 cubemap_offset;          // 12 bytes
      float _pad_offset;                 // 4 bytes
   };
   static_assert(sizeof(Constants) == 192);

   [[nodiscard]] auto pack_constants(const Sky_dome_input& input) const noexcept -> Constants;

   Sky_dome_params _params;

   Com_ptr<ID3D11Device5> _device;
   Com_ptr<ID3D11Buffer> _constant_buffer;
   Com_ptr<ID3D11VertexShader> _vs;
   Com_ptr<ID3D11PixelShader> _ps;
   Com_ptr<ID3D11BlendState> _blend_state;
   Com_ptr<ID3D11DepthStencilState> _no_depth_state;
   Com_ptr<ID3D11SamplerState> _linear_clamp_sampler;
};

}

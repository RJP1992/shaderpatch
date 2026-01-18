#pragma once

#include "../shader/database.hpp"
#include "com_ptr.hpp"
#include "postprocess_params.hpp"
#include "profiler.hpp"

#include <d3d11_4.h>
#include <glm/glm.hpp>

namespace sp::effects {

struct Cubemap_debug_input {
   glm::mat4 view_matrix;
   glm::mat4 projection_matrix;
   glm::vec3 camera_position;
   UINT width;
   UINT height;
   ID3D11ShaderResourceView* cubemap_srv;
   ID3D11ShaderResourceView* depth_near;
   ID3D11ShaderResourceView* depth_far;
};

class Cubemap_debug {
public:
   Cubemap_debug(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept;

   void render(ID3D11DeviceContext1& dc,
               Profiler& profiler,
               ID3D11RenderTargetView& output_rtv,
               const Cubemap_debug_input& input,
               const Fog_params& fog_params) noexcept;

private:
   // GPU constant buffer structure (must match HLSL cubemap_debug.fx)
   struct alignas(16) Constants {
      glm::mat4 inv_view_proj;           // 64 bytes

      glm::vec3 camera_position;         // 12 bytes
      float debug_distance;              // 4 bytes

      // Cubemap alignment transform (HLSL float3x3 is stored as 3 rows of float4)
      glm::vec4 cubemap_rotation_row0;   // 16 bytes
      glm::vec4 cubemap_rotation_row1;   // 16 bytes
      glm::vec4 cubemap_rotation_row2;   // 16 bytes
      glm::vec3 cubemap_scale;           // 12 bytes
      float _pad_scale;                  // 4 bytes
      glm::vec3 cubemap_offset;          // 12 bytes
      float _pad_offset;                 // 4 bytes

      int render_at_infinity;            // 4 bytes
      int show_grid;                     // 4 bytes
      float grid_thickness;              // 4 bytes
      float _pad;                        // 4 bytes
   };
   static_assert(sizeof(Constants) == 176);

   [[nodiscard]] auto pack_constants(const Fog_params& params,
                                     const Cubemap_debug_input& input) const noexcept -> Constants;

   Com_ptr<ID3D11Device5> _device;
   Com_ptr<ID3D11Buffer> _constant_buffer;
   Com_ptr<ID3D11VertexShader> _vs;
   Com_ptr<ID3D11PixelShader> _ps;
   Com_ptr<ID3D11BlendState> _blend_state;
   Com_ptr<ID3D11DepthStencilState> _no_depth_state;
   Com_ptr<ID3D11SamplerState> _linear_clamp_sampler;
};

}

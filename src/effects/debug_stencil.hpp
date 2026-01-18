#pragma once

#include "../shader/database.hpp"
#include "com_ptr.hpp"

#include <d3d11_4.h>

namespace sp::effects {

struct Debug_stencil_params {
   bool enabled = false;
   int mode = 1;        // 0=depth, 1=stencil color, 2=stencil raw, 3=combined
   bool use_near = true; // true=nearscene, false=farscene
};

class Debug_stencil {
public:
   Debug_stencil(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept;

   void render(ID3D11DeviceContext1& dc,
               ID3D11RenderTargetView& output_rtv,
               UINT width, UINT height,
               ID3D11ShaderResourceView* depth_srv,
               ID3D11ShaderResourceView* stencil_srv) noexcept;

   auto params() const noexcept -> const Debug_stencil_params& { return _params; }
   auto params() noexcept -> Debug_stencil_params& { return _params; }

private:
   struct alignas(16) Constants {
      int mode;
      int use_near;
      float _pad[2];
   };
   static_assert(sizeof(Constants) == 16);

   Debug_stencil_params _params;

   Com_ptr<ID3D11Device5> _device;
   Com_ptr<ID3D11Buffer> _constant_buffer;
   Com_ptr<ID3D11VertexShader> _vs;
   Com_ptr<ID3D11PixelShader> _ps;
   Com_ptr<ID3D11BlendState> _blend_state;
   Com_ptr<ID3D11DepthStencilState> _depth_state;
   Com_ptr<ID3D11SamplerState> _sampler;
};

}

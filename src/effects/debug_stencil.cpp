#include "debug_stencil.hpp"
#include "../core/d3d11_helpers.hpp"

namespace sp::effects {

using namespace std::literals;

Debug_stencil::Debug_stencil(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept
   : _device{device},
     _constant_buffer{core::create_dynamic_constant_buffer(*device, sizeof(Constants))}
{
   // Get shaders - use shared postprocess vertex shader
   _vs = std::get<0>(shaders.vertex("postprocess"sv).entrypoint("main_vs"sv));
   _ps = shaders.pixel("debug_stencil"sv).entrypoint("main_ps"sv);

   // Create blend state (opaque)
   {
      CD3D11_BLEND_DESC desc{CD3D11_DEFAULT{}};
      desc.RenderTarget[0].BlendEnable = false;
      desc.RenderTarget[0].RenderTargetWriteMask = 0b1111;
      device->CreateBlendState(&desc, _blend_state.clear_and_assign());
   }

   // Create depth stencil state (disabled)
   {
      CD3D11_DEPTH_STENCIL_DESC desc{CD3D11_DEFAULT{}};
      desc.DepthEnable = false;
      desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
      desc.StencilEnable = false;
      device->CreateDepthStencilState(&desc, _depth_state.clear_and_assign());
   }

   // Create point sampler
   {
      CD3D11_SAMPLER_DESC desc{CD3D11_DEFAULT{}};
      desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
      desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
      desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      device->CreateSamplerState(&desc, _sampler.clear_and_assign());
   }
}

void Debug_stencil::render(ID3D11DeviceContext1& dc,
                           ID3D11RenderTargetView& output_rtv,
                           UINT width, UINT height,
                           ID3D11ShaderResourceView* depth_srv,
                           ID3D11ShaderResourceView* stencil_srv) noexcept
{
   if (!_params.enabled) return;

   // Update constants
   Constants cb{};
   cb.mode = _params.mode;
   cb.use_near = _params.use_near ? 1 : 0;
   core::update_dynamic_buffer(dc, *_constant_buffer, cb);

   // Set viewport
   const D3D11_VIEWPORT viewport{
      0.0f, 0.0f,
      static_cast<float>(width),
      static_cast<float>(height),
      0.0f, 1.0f
   };
   dc.RSSetViewports(1, &viewport);

   // Set pipeline
   dc.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   dc.IASetInputLayout(nullptr);
   dc.VSSetShader(_vs.get(), nullptr, 0);
   dc.PSSetShader(_ps.get(), nullptr, 0);

   auto* cb_ptr = _constant_buffer.get();
   dc.PSSetConstantBuffers(0, 1, &cb_ptr);

   // Bind depth and stencil textures
   ID3D11ShaderResourceView* srvs[2] = { depth_srv, stencil_srv };
   dc.PSSetShaderResources(0, 2, srvs);

   auto* sampler = _sampler.get();
   dc.PSSetSamplers(0, 1, &sampler);

   // Set render target
   auto* rtv = &output_rtv;
   dc.OMSetRenderTargets(1, &rtv, nullptr);
   dc.OMSetBlendState(_blend_state.get(), nullptr, 0xFFFFFFFF);
   dc.OMSetDepthStencilState(_depth_state.get(), 0);

   // Draw fullscreen triangle
   dc.Draw(3, 0);

   // Unbind SRVs
   ID3D11ShaderResourceView* null_srvs[2] = { nullptr, nullptr };
   dc.PSSetShaderResources(0, 2, null_srvs);
}

}

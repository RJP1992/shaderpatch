#include "sky_dome.hpp"
#include "../core/d3d11_helpers.hpp"
#include "helpers.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace sp::effects {

namespace {

auto make_premultiplied_blend_state(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11BlendState>
{
   CD3D11_BLEND_DESC desc{CD3D11_DEFAULT{}};

   desc.RenderTarget[0].BlendEnable = true;
   desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;  // Premultiplied
   desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
   desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
   desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
   desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
   desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
   desc.RenderTarget[0].RenderTargetWriteMask = 0b1111;

   Com_ptr<ID3D11BlendState> blend_state;
   device.CreateBlendState(&desc, blend_state.clear_and_assign());

   return blend_state;
}

auto make_no_depth_state(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11DepthStencilState>
{
   CD3D11_DEPTH_STENCIL_DESC desc{CD3D11_DEFAULT{}};

   desc.DepthEnable = false;
   desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
   desc.StencilEnable = false;

   Com_ptr<ID3D11DepthStencilState> depth_state;
   device.CreateDepthStencilState(&desc, depth_state.clear_and_assign());

   return depth_state;
}

auto make_linear_clamp_sampler(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11SamplerState>
{
   CD3D11_SAMPLER_DESC desc{CD3D11_DEFAULT{}};

   desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
   desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
   desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
   desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

   Com_ptr<ID3D11SamplerState> sampler;
   device.CreateSamplerState(&desc, sampler.clear_and_assign());

   return sampler;
}

}

Sky_dome::Sky_dome(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept
   : _device{device},
     _constant_buffer{core::create_dynamic_constant_buffer(*device, sizeof(Constants))},
     _vs{std::get<0>(shaders.vertex("postprocess"sv).entrypoint("main_vs"sv))},
     _ps{shaders.pixel("sky_dome"sv).entrypoint("main_ps"sv)},
     _blend_state{make_premultiplied_blend_state(*device)},
     _no_depth_state{make_no_depth_state(*device)},
     _linear_clamp_sampler{make_linear_clamp_sampler(*device)}
{
}

auto Sky_dome::pack_constants(const Sky_dome_input& input) const noexcept -> Constants
{
   Constants cb{};

   // Compute inverse view-projection matrix
   glm::mat4 view_proj = input.projection_matrix * input.view_matrix;
   cb.inv_view_proj = glm::inverse(view_proj);

   cb.camera_position = input.camera_position;
   cb.atmosphere_density = _params.atmosphere_density;

   cb.horizon_shift = _params.horizon_shift;
   cb.horizon_start = _params.horizon_start;
   cb.horizon_blend = _params.horizon_blend;
   cb.fade_start_height = _params.fade_start_height;

   cb.fade_end_height = _params.fade_end_height;
   cb.tint = _params.tint;

   // Build rotation matrix from Euler angles
   const glm::vec3 rot_rad = glm::radians(_params.cubemap_rotation);
   const glm::mat3 rotation = glm::mat3(glm::yawPitchRoll(rot_rad.y, rot_rad.x, rot_rad.z));

   // Store as 3 rows (HLSL float3x3 layout with padding)
   cb.cubemap_rotation_row0 = glm::vec4(rotation[0], 0.0f);
   cb.cubemap_rotation_row1 = glm::vec4(rotation[1], 0.0f);
   cb.cubemap_rotation_row2 = glm::vec4(rotation[2], 0.0f);

   cb.cubemap_scale = _params.cubemap_scale;
   cb.cubemap_offset = _params.cubemap_offset;

   return cb;
}

void Sky_dome::render(ID3D11DeviceContext1& dc,
                      Profiler& profiler,
                      ID3D11RenderTargetView& output_rtv,
                      const Sky_dome_input& input) noexcept
{
   if (!_params.enabled) return;
   if (!input.atmosphere_cubemap_srv) return;  // Need a cubemap to render

   Profile profile{profiler, dc, "Sky Dome"};

   // Update constants
   Constants cb = pack_constants(input);
   core::update_dynamic_buffer(dc, *_constant_buffer, cb);

   // Set viewport
   const D3D11_VIEWPORT viewport{
      0.0f, 0.0f,
      static_cast<float>(input.width),
      static_cast<float>(input.height),
      0.0f, 1.0f
   };
   dc.RSSetViewports(1, &viewport);

   // Set pipeline state
   dc.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   dc.IASetInputLayout(nullptr);
   dc.VSSetShader(_vs.get(), nullptr, 0);
   dc.PSSetShader(_ps.get(), nullptr, 0);

   auto* cb_ptr = _constant_buffer.get();
   dc.PSSetConstantBuffers(1, 1, &cb_ptr);  // b1 to match shader

   // Bind textures: t0=atmosphere_cubemap, t1=depth_near, t2=depth_far
   ID3D11ShaderResourceView* srvs[3] = {
      input.atmosphere_cubemap_srv,
      input.depth_near,
      input.depth_far
   };
   dc.PSSetShaderResources(0, 3, srvs);

   auto* sampler = _linear_clamp_sampler.get();
   dc.PSSetSamplers(0, 1, &sampler);

   // Render target with premultiplied alpha blending
   auto* rtv = &output_rtv;
   dc.OMSetRenderTargets(1, &rtv, nullptr);
   dc.OMSetBlendState(_blend_state.get(), nullptr, 0xFFFFFFFF);
   dc.OMSetDepthStencilState(_no_depth_state.get(), 0);

   // Draw fullscreen triangle
   dc.Draw(3, 0);
}

}

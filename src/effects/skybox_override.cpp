#include "skybox_override.hpp"
#include "../core/d3d11_helpers.hpp"
#include "helpers.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace sp::effects {

using namespace std::literals;

namespace {

auto make_opaque_blend_state(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11BlendState>
{
   CD3D11_BLEND_DESC desc{CD3D11_DEFAULT{}};

   desc.RenderTarget[0].BlendEnable = false;
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

Skybox_override::Skybox_override(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept
   : _device{device},
     _constant_buffer{core::create_dynamic_constant_buffer(*device, sizeof(Constants))},
     _vs{std::get<0>(shaders.vertex("postprocess"sv).entrypoint("main_vs"sv))},
     _ps{shaders.pixel("postprocess_skybox"sv).entrypoint("main_ps"sv)},
     _opaque_blend_state{make_opaque_blend_state(*device)},
     _no_depth_state{make_no_depth_state(*device)},
     _linear_clamp_sampler{make_linear_clamp_sampler(*device)}
{
}

auto Skybox_override::pack_constants(const Cubemap_alignment& cubemap_alignment,
                                     const Skybox_override_input& input,
                                     bool has_sky_cubemap) const noexcept -> Constants
{
   Constants cb{};

   // Compute inverse view-projection matrix
   glm::mat4 view_proj = input.projection_matrix * input.view_matrix;
   cb.inv_view_proj = glm::inverse(view_proj);

   cb.camera_position = input.camera_position;
   cb.sky_distance_threshold = _params.sky_distance_threshold;

   // Use shared cubemap alignment
   const auto packed = pack_cubemap_transform(cubemap_alignment);
   cb.cubemap_rotation_row0 = packed.rotation_row0;
   cb.cubemap_rotation_row1 = packed.rotation_row1;
   cb.cubemap_rotation_row2 = packed.rotation_row2;
   cb.cubemap_scale = packed.scale;
   cb.cubemap_offset = packed.offset;

   // DOF-style depth-to-distance conversion (more robust than world reconstruction)
   cb.proj_from_view_m33 = input.projection_matrix[2][2];
   cb.proj_from_view_m43 = input.projection_matrix[3][2];

   // Atmosphere params
   cb.atmos_density = _params.atmos_density;
   cb.horizon_shift = _params.horizon_shift;
   cb.horizon_start = _params.horizon_start;
   cb.horizon_blend = _params.horizon_blend;

   cb.tint = _params.tint;
   cb.use_atmosphere = has_sky_cubemap ? 1.0f : 0.0f;

   cb.debug_mode = static_cast<float>(_params.debug_mode);

   return cb;
}

void Skybox_override::render(ID3D11DeviceContext1& dc,
                             Profiler& profiler,
                             ID3D11RenderTargetView& output_rtv,
                             const Cubemap_alignment& cubemap_alignment,
                             const Skybox_override_input& input) noexcept
{
   if (!_params.enabled) return;
   if (!input.ground_cubemap_srv) return;  // Need a ground cubemap to render

   Profile profile{profiler, dc, "Skybox Override"};

   // Update constants
   bool has_sky_cubemap = input.sky_cubemap_srv != nullptr;
   Constants cb = pack_constants(cubemap_alignment, input, has_sky_cubemap);
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

   // Bind textures: t0=ground_cubemap, t1=sky_cubemap, t2=depth_near, t3=depth_far, t4=stencil_near, t5=stencil_far
   ID3D11ShaderResourceView* srvs[6] = {
      input.ground_cubemap_srv,
      input.sky_cubemap_srv,  // May be nullptr
      input.depth_near,
      input.depth_far,
      input.stencil_near,
      input.stencil_far
   };
   dc.PSSetShaderResources(0, 6, srvs);

   auto* sampler = _linear_clamp_sampler.get();
   dc.PSSetSamplers(0, 1, &sampler);

   // Render target with opaque blending (replace)
   auto* rtv = &output_rtv;
   dc.OMSetRenderTargets(1, &rtv, nullptr);
   dc.OMSetBlendState(_opaque_blend_state.get(), nullptr, 0xFFFFFFFF);
   dc.OMSetDepthStencilState(_no_depth_state.get(), 0);

   // Draw fullscreen triangle
   dc.Draw(3, 0);
}

}

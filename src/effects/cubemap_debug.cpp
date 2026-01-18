#include "cubemap_debug.hpp"
#include "../core/d3d11_helpers.hpp"
#include "helpers.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace sp::effects {

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

Cubemap_debug::Cubemap_debug(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept
   : _device{device},
     _constant_buffer{core::create_dynamic_constant_buffer(*device, sizeof(Constants))},
     _vs{std::get<0>(shaders.vertex("postprocess"sv).entrypoint("main_vs"sv))},
     _ps{shaders.pixel("cubemap_debug"sv).entrypoint("main_ps"sv)},
     _blend_state{make_opaque_blend_state(*device)},
     _no_depth_state{make_no_depth_state(*device)},
     _linear_clamp_sampler{make_linear_clamp_sampler(*device)}
{
}

auto Cubemap_debug::pack_constants(const Fog_params& params,
                                   const Cubemap_debug_input& input) const noexcept -> Constants
{
   Constants cb{};

   // Compute inverse view-projection matrix
   glm::mat4 view_proj = input.projection_matrix * input.view_matrix;
   cb.inv_view_proj = glm::inverse(view_proj);

   cb.camera_position = input.camera_position;
   cb.debug_distance = params.cubemap_debug_distance;

   // Build rotation matrix from Euler angles (same as fog shader)
   const glm::vec3 rot_rad = glm::radians(params.cubemap_rotation);
   const glm::mat3 rotation = glm::mat3(glm::yawPitchRoll(rot_rad.y, rot_rad.x, rot_rad.z));

   // Store as 3 rows (HLSL float3x3 layout with padding)
   cb.cubemap_rotation_row0 = glm::vec4(rotation[0], 0.0f);
   cb.cubemap_rotation_row1 = glm::vec4(rotation[1], 0.0f);
   cb.cubemap_rotation_row2 = glm::vec4(rotation[2], 0.0f);

   cb.cubemap_scale = params.cubemap_scale;
   cb.cubemap_offset = params.cubemap_offset;

   cb.render_at_infinity = params.cubemap_debug_at_infinity ? 1 : 0;
   cb.show_grid = 1;  // Always show grid for debug visualization
   cb.grid_thickness = 0.05f;

   return cb;
}

void Cubemap_debug::render(ID3D11DeviceContext1& dc,
                           Profiler& profiler,
                           ID3D11RenderTargetView& output_rtv,
                           const Cubemap_debug_input& input,
                           const Fog_params& fog_params) noexcept
{
   if (!fog_params.cubemap_debug_enabled) return;
   if (!input.cubemap_srv) return;  // Need a cubemap to render

   Profile profile{profiler, dc, "Cubemap Debug"};

   // Update constants
   Constants cb = pack_constants(fog_params, input);
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
   dc.PSSetConstantBuffers(0, 1, &cb_ptr);

   // Bind textures: t0=cubemap, t1=depth_near, t2=depth_far
   ID3D11ShaderResourceView* srvs[3] = {
      input.cubemap_srv,
      input.depth_near,
      input.depth_far
   };
   dc.PSSetShaderResources(0, 3, srvs);

   auto* sampler = _linear_clamp_sampler.get();
   dc.PSSetSamplers(0, 1, &sampler);

   // Render target with opaque blend (debug writes directly over scene)
   auto* rtv = &output_rtv;
   dc.OMSetRenderTargets(1, &rtv, nullptr);
   dc.OMSetBlendState(_blend_state.get(), nullptr, 0xFFFFFFFF);
   dc.OMSetDepthStencilState(_no_depth_state.get(), 0);

   // Draw fullscreen triangle
   dc.Draw(3, 0);
}

}

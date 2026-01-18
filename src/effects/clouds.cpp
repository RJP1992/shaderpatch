
#include "clouds.hpp"
#include "../core/d3d11_helpers.hpp"
#include "helpers.hpp"

#include <cmath>

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

auto make_aniso_wrap_sampler(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11SamplerState>
{
   CD3D11_SAMPLER_DESC desc{CD3D11_DEFAULT{}};

   desc.Filter = D3D11_FILTER_ANISOTROPIC;
   desc.MaxAnisotropy = 16;
   desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
   desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
   desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

   Com_ptr<ID3D11SamplerState> sampler;
   device.CreateSamplerState(&desc, sampler.clear_and_assign());

   return sampler;
}

}

Clouds::Clouds(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept
   : _device{device},
     _constant_buffer{core::create_dynamic_constant_buffer(*device, sizeof(Constants))},
     _vs{std::get<0>(shaders.vertex("postprocess"sv).entrypoint("main_vs"sv))},
     _ps{shaders.pixel("clouds"sv).entrypoint("main_ps"sv)},
     _blend_state{make_premultiplied_blend_state(*device)},
     _no_depth_state{make_no_depth_state(*device)},
     _aniso_sampler{make_aniso_wrap_sampler(*device)}
{
}

float Clouds::calculate_fog_boost(float camera_y) const noexcept
{
   if (!_params.enabled) return 0.0f;

   float max_boost = 0.0f;

   for (const auto& layer : _params.layers) {
      if (!layer.enabled || layer.fog_boost_max <= 0.0f) continue;

      float half_thickness = layer.thickness * 0.5f;
      float dist_to_layer = std::abs(camera_y - layer.height);

      if (dist_to_layer < half_thickness) {
         // Inside this layer's vertical extent
         float penetration = 1.0f - (dist_to_layer / half_thickness);
         float layer_boost = penetration * layer.fog_boost_max;
         max_boost = std::max(max_boost, layer_boost);
      }
   }

   return max_boost * _params.global_fog_boost_scale;
}

auto Clouds::pack_constants(const Cloud_params& params,
                            const Cloud_input& input) const noexcept -> Constants
{
   Constants cb{};

   // Compute inverse view-projection matrix
   glm::mat4 view_proj = input.projection_matrix * input.view_matrix;
   cb.inverse_view_projection = glm::inverse(view_proj);

   cb.camera_position = input.camera_position;
   cb.time = input.time;

   cb.sun_direction = input.sun_direction;
   cb.horizon_fade_start = params.horizon_fade_start;

   cb.sun_color = input.sun_color;
   cb.horizon_fade_end = params.horizon_fade_end;

   cb.distance_fade_start = params.distance_fade_start;
   cb.distance_fade_end = params.distance_fade_end;
   cb.near_fade_start = params.near_fade_start;
   cb.near_fade_end = params.near_fade_end;

   cb.curvature_center = params.curvature_center;

   // Pack layers with per-layer appearance settings
   for (int i = 0; i < 3; ++i) {
      const auto& src = params.layers[i];
      auto& dst = cb.layers[i];

      dst.height = src.height;
      dst.thickness = src.thickness;
      dst.scale = src.scale;
      dst.density = src.enabled ? src.density : 0.0f;  // Disable via density

      dst.scroll_speed = src.scroll_speed;
      dst.scroll_angle = src.scroll_angle;
      dst.fog_boost_max = src.fog_boost_max;
      dst.curvature = src.curvature;

      dst.cloud_threshold = src.cloud_threshold;
      dst.cloud_softness = src.cloud_softness;
      dst.sun_color_influence = src.sun_color_influence;
      dst.lighting_wrap = src.lighting_wrap;

      dst.color_lit = src.color_lit;
      dst.cloud_brightness = src.cloud_brightness;

      dst.color_dark = src.color_dark;
      dst.min_brightness = src.min_brightness;

      dst.octave_weights = src.octave_weights;
      dst.octave_blend = src.octave_blend;
      dst.use_normal_lighting = src.use_normal_lighting ? 1.0f : 0.0f;
   }

   return cb;
}

void Clouds::render(ID3D11DeviceContext1& dc,
                    Profiler& profiler,
                    const core::Shader_resource_database& textures,
                    ID3D11RenderTargetView& output_rtv,
                    const Cloud_input& input) noexcept
{
   if (!_params.enabled) return;

   Profile profile{profiler, dc, "Clouds"};

   // Update constants
   Constants cb = pack_constants(_params, input);
   core::update_dynamic_buffer(dc, *_constant_buffer, cb);

   // Get cloud octave texture (lazy load)
   if (!_cloud_octaves_srv) {
      _cloud_octaves_srv = textures.at_if("_SP_BUILTIN_cloud_octaves"sv);

      // Fallback to perlin if octave texture not available
      if (!_cloud_octaves_srv) {
         _cloud_octaves_srv = textures.at_if("_SP_BUILTIN_perlin"sv);
      }
   }

   if (!_cloud_octaves_srv) return;  // Can't render without noise texture

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

   // Bind textures: t0=cloud_octaves (RGBA), t1=depth_near, t2=depth_far
   ID3D11ShaderResourceView* srvs[3] = {
      _cloud_octaves_srv.get(),
      input.depth_near,
      input.depth_far
   };
   dc.PSSetShaderResources(0, 3, srvs);

   auto* sampler = _aniso_sampler.get();
   dc.PSSetSamplers(0, 1, &sampler);

   // Blend state: premultiplied alpha over existing scene
   auto* rtv = &output_rtv;
   dc.OMSetRenderTargets(1, &rtv, nullptr);
   dc.OMSetBlendState(_blend_state.get(), nullptr, 0xFFFFFFFF);
   dc.OMSetDepthStencilState(_no_depth_state.get(), 0);

   // Draw fullscreen triangle (shader handles all 3 layers)
   dc.Draw(3, 0);
}

}

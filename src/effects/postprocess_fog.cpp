
#include "postprocess_fog.hpp"
#include "utility.hpp"

namespace sp::effects {

namespace {

auto make_point_sampler(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11SamplerState>
{
   const D3D11_SAMPLER_DESC desc{
      .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
      .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
      .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
      .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
      .MipLODBias = 0.0f,
      .MaxAnisotropy = 1,
      .ComparisonFunc = D3D11_COMPARISON_NEVER,
      .BorderColor = {0.0f, 0.0f, 0.0f, 0.0f},
      .MinLOD = 0.0f,
      .MaxLOD = D3D11_FLOAT32_MAX,
   };

   Com_ptr<ID3D11SamplerState> sampler;
   device.CreateSamplerState(&desc, sampler.clear_and_assign());

   return sampler;
}

auto make_constant_buffer(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11Buffer>
{
   const D3D11_BUFFER_DESC desc{
      .ByteWidth = sizeof(core::cb::Fog),
      .Usage = D3D11_USAGE_DYNAMIC,
      .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
      .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
      .MiscFlags = 0,
      .StructureByteStride = 0,
   };

   Com_ptr<ID3D11Buffer> buffer;
   device.CreateBuffer(&desc, nullptr, buffer.clear_and_assign());

   return buffer;
}

}

Postprocess_fog::Postprocess_fog(Com_ptr<ID3D11Device5> device,
                                 shader::Database& shaders) noexcept
   : _device{device},
     _vs{std::get<0>(shaders.vertex("postprocess_fog"sv).entrypoint("main_vs"sv))},
     _ps{shaders.pixel("postprocess_fog"sv).entrypoint("main_ps"sv)},
     _ps_debug_depth{shaders.pixel("postprocess_fog"sv).entrypoint("debug_depth_ps"sv)},
     _ps_debug_world_y{shaders.pixel("postprocess_fog"sv).entrypoint("debug_world_y_ps"sv)},
     _constant_buffer{make_constant_buffer(*device)},
     _point_sampler{make_point_sampler(*device)}
{
}

void Postprocess_fog::apply(ID3D11DeviceContext4& dc, const Postprocess_fog_input& input,
                            const core::cb::Fog& fog_constants, Profiler& profiler) noexcept
{
   if (!_enabled) return;

   Profile profile{profiler, dc, "SWBF3 Post-Process Fog"};

   // Update constant buffer
   D3D11_MAPPED_SUBRESOURCE mapped;
   if (SUCCEEDED(dc.Map(_constant_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      std::memcpy(mapped.pData, &fog_constants, sizeof(fog_constants));
      dc.Unmap(_constant_buffer.get(), 0);
   }

   // Save current state (we'll clear it)
   dc.ClearState();

   // Set up viewport
   const D3D11_VIEWPORT viewport = {
      .TopLeftX = 0.0f,
      .TopLeftY = 0.0f,
      .Width = static_cast<float>(input.width),
      .Height = static_cast<float>(input.height),
      .MinDepth = 0.0f,
      .MaxDepth = 1.0f,
   };

   // Bind resources
   auto* rtv = &input.rtv;
   ID3D11ShaderResourceView* srvs[] = {&input.scene_srv, &input.depth_srv};
   ID3D11SamplerState* samplers[] = {_point_sampler.get()};
   ID3D11Buffer* cbs[] = {_constant_buffer.get()};

   // Select pixel shader based on debug mode
   ID3D11PixelShader* ps = _ps.get();
   switch (_debug_mode) {
   case Fog_debug_mode::depth:
      ps = _ps_debug_depth.get();
      break;
   case Fog_debug_mode::world_y:
      ps = _ps_debug_world_y.get();
      break;
   default:
      break;
   }

   dc.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   dc.VSSetShader(_vs.get(), nullptr, 0);
   dc.RSSetViewports(1, &viewport);
   dc.PSSetShader(ps, nullptr, 0);
   dc.PSSetShaderResources(0, 2, srvs);
   dc.PSSetSamplers(0, 1, samplers);
   dc.PSSetConstantBuffers(4, 1, cbs);  // b4 for fog constants
   dc.OMSetRenderTargets(1, &rtv, nullptr);

   // Draw fullscreen triangle
   dc.Draw(3, 0);
}

}

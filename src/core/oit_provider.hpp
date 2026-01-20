#pragma once

#include "../shader/database.hpp"
#include "com_ptr.hpp"
#include "constant_buffers.hpp"

#include <d3d11_4.h>

namespace sp::core {

class OIT_provider {
public:
   OIT_provider(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept;

   ~OIT_provider() noexcept = default;

   OIT_provider(const OIT_provider&) = delete;
   OIT_provider& operator=(const OIT_provider&) = delete;

   OIT_provider(OIT_provider&&) = delete;
   OIT_provider& operator=(OIT_provider&&) = delete;

   void prepare_resources(ID3D11DeviceContext4& dc, ID3D11Texture2D& opaque_texture,
                          ID3D11RenderTargetView& opaque_rtv) noexcept;

   void clear_resources() noexcept;

   void resolve(ID3D11DeviceContext4& dc, const cb::Fog* fog_constants = nullptr) noexcept;

   auto uavs() const noexcept -> std::array<ID3D11UnorderedAccessView*, 3>;

   bool enabled() const noexcept;

   static bool usable(ID3D11Device5& device) noexcept;

private:
   void update_resources(ID3D11Texture2D& opaque_texture,
                         ID3D11RenderTargetView& opaque_rtv) noexcept;

   const Com_ptr<ID3D11Device5> _device;
   const Com_ptr<ID3D11VertexShader> _vs;
   const Com_ptr<ID3D11PixelShader> _ps;
   const Com_ptr<ID3D11BlendState1> _composite_blendstate = [this] {
      CD3D11_BLEND_DESC1 desc{CD3D11_DEFAULT{}};

      desc.RenderTarget[0].BlendEnable = true;
      desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
      desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
      desc.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_ALPHA;
      desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_SRC_ALPHA;
      desc.RenderTarget[0].RenderTargetWriteMask = 0b111;

      Com_ptr<ID3D11BlendState1> blendstate;

      _device->CreateBlendState1(&desc, blendstate.clear_and_assign());

      return blendstate;
   }();

   // OIT resolve constants (b5)
   struct alignas(16) OIT_resolve_constants {
      glm::vec2 screen_size{0.0f, 0.0f};
      std::uint32_t fog_enabled{0};
      float _padding{0.0f};
   };

   Com_ptr<ID3D11Buffer> _resolve_cb = [this] {
      D3D11_BUFFER_DESC desc{};
      desc.ByteWidth = sizeof(OIT_resolve_constants);
      desc.Usage = D3D11_USAGE_DYNAMIC;
      desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

      Com_ptr<ID3D11Buffer> buffer;
      _device->CreateBuffer(&desc, nullptr, buffer.clear_and_assign());
      return buffer;
   }();

   Com_ptr<ID3D11Buffer> _fog_cb = [this] {
      D3D11_BUFFER_DESC desc{};
      desc.ByteWidth = sizeof(cb::Fog);
      desc.Usage = D3D11_USAGE_DYNAMIC;
      desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

      Com_ptr<ID3D11Buffer> buffer;
      _device->CreateBuffer(&desc, nullptr, buffer.clear_and_assign());
      return buffer;
   }();

   Com_ptr<ID3D11Texture2D> _opaque_texture;
   Com_ptr<ID3D11RenderTargetView> _opaque_rtv;
   Com_ptr<ID3D11UnorderedAccessView> _clear_uav;
   Com_ptr<ID3D11ShaderResourceView> _clear_srv;
   Com_ptr<ID3D11UnorderedAccessView> _depth_uav;
   Com_ptr<ID3D11ShaderResourceView> _depth_srv;
   Com_ptr<ID3D11UnorderedAccessView> _color_uav;
   Com_ptr<ID3D11ShaderResourceView> _color_srv;

   const bool _usable = usable(*_device);
};
}

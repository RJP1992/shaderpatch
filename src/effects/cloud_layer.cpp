
#include "cloud_layer.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace sp::effects {

using namespace std::literals;

namespace {

auto make_wrap_sampler(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11SamplerState>
{
   const D3D11_SAMPLER_DESC desc{
      .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
      .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
      .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
      .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
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
      .ByteWidth = sizeof(core::cb::CloudLayers),
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

auto make_blend_state(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11BlendState>
{
   D3D11_BLEND_DESC desc{};
   desc.RenderTarget[0].BlendEnable = TRUE;
   desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
   desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
   desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
   desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
   desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
   desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
   desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

   Com_ptr<ID3D11BlendState> state;
   device.CreateBlendState(&desc, state.clear_and_assign());

   return state;
}

// Simple hash for noise generation
uint32_t hash(uint32_t x)
{
   x ^= x >> 16;
   x *= 0x45d9f3b;
   x ^= x >> 16;
   x *= 0x45d9f3b;
   x ^= x >> 16;
   return x;
}

// Generate tileable value noise at a given position and frequency
float tileable_noise(int x, int y, int freq, uint32_t seed)
{
   // Wrap coordinates for tileability
   int wx = ((x % freq) + freq) % freq;
   int wy = ((y % freq) + freq) % freq;

   // Hash the wrapped coordinate
   uint32_t h = hash(static_cast<uint32_t>(wx) +
                      static_cast<uint32_t>(wy) * 65537u + seed);
   return static_cast<float>(h & 0xFFFF) / 65535.0f;
}

// Bilinear interpolated tileable noise
float smooth_noise(float fx, float fy, int freq, uint32_t seed)
{
   int x0 = static_cast<int>(std::floor(fx));
   int y0 = static_cast<int>(std::floor(fy));
   float tx = fx - std::floor(fx);
   float ty = fy - std::floor(fy);

   // Smoothstep interpolation
   tx = tx * tx * (3.0f - 2.0f * tx);
   ty = ty * ty * (3.0f - 2.0f * ty);

   float n00 = tileable_noise(x0, y0, freq, seed);
   float n10 = tileable_noise(x0 + 1, y0, freq, seed);
   float n01 = tileable_noise(x0, y0 + 1, freq, seed);
   float n11 = tileable_noise(x0 + 1, y0 + 1, freq, seed);

   float nx0 = n00 + (n10 - n00) * tx;
   float nx1 = n01 + (n11 - n01) * tx;

   return nx0 + (nx1 - nx0) * ty;
}

// Generate a single channel of multi-octave tileable noise
float fbm_noise(float x, float y, int base_freq, uint32_t seed)
{
   float value = 0.0f;
   float amplitude = 1.0f;
   float total_amp = 0.0f;
   int freq = base_freq;

   for (int oct = 0; oct < 4; oct++) {
      value += smooth_noise(x * freq / 256.0f,
                           y * freq / 256.0f,
                           freq, seed + oct * 1000) * amplitude;
      total_amp += amplitude;
      amplitude *= 0.5f;
      freq *= 2;
   }

   return value / total_amp;
}

}

Cloud_layer::Cloud_layer(Com_ptr<ID3D11Device5> device,
                         shader::Database& shaders) noexcept
   : _device{device},
     _vs{std::get<0>(shaders.vertex("cloud_layer"sv).entrypoint("main_vs"sv))},
     _ps{shaders.pixel("cloud_layer"sv).entrypoint("main_ps"sv)},
     _constant_buffer{make_constant_buffer(*device)},
     _wrap_sampler{make_wrap_sampler(*device)},
     _blend_state{make_blend_state(*device)}
{
   generate_noise_textures();
}

void Cloud_layer::generate_noise_textures() noexcept
{
   constexpr int size = 256;

   // Generate two noise textures, each with 4 channels at different frequencies
   for (int tex_idx = 0; tex_idx < 2; tex_idx++) {
      std::array<uint8_t, size * size * 4> pixels;

      for (int y = 0; y < size; y++) {
         for (int x = 0; x < size; x++) {
            int idx = (y * size + x) * 4;

            // Each channel uses a different base frequency and seed
            // This creates 4 octaves of noise per texture
            uint32_t base_seed = tex_idx * 10000;
            int freqs[] = {4, 8, 16, 32}; // Different scales per channel

            for (int ch = 0; ch < 4; ch++) {
               float n = fbm_noise(static_cast<float>(x), static_cast<float>(y),
                                   freqs[ch], base_seed + ch * 2000);
               pixels[idx + ch] = static_cast<uint8_t>(std::clamp(n * 255.0f, 0.0f, 255.0f));
            }
         }
      }

      // Create D3D11 texture
      D3D11_TEXTURE2D_DESC tex_desc{};
      tex_desc.Width = size;
      tex_desc.Height = size;
      tex_desc.MipLevels = 1;
      tex_desc.ArraySize = 1;
      tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      tex_desc.SampleDesc.Count = 1;
      tex_desc.SampleDesc.Quality = 0;
      tex_desc.Usage = D3D11_USAGE_IMMUTABLE;
      tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

      D3D11_SUBRESOURCE_DATA init_data{};
      init_data.pSysMem = pixels.data();
      init_data.SysMemPitch = size * 4;

      Com_ptr<ID3D11Texture2D> texture;
      _device->CreateTexture2D(&tex_desc, &init_data, texture.clear_and_assign());

      Com_ptr<ID3D11ShaderResourceView> srv;
      _device->CreateShaderResourceView(texture.get(), nullptr, srv.clear_and_assign());

      if (tex_idx == 0)
         _noise_tex0 = std::move(srv);
      else
         _noise_tex1 = std::move(srv);
   }
}

void Cloud_layer::set_noise_textures(Com_ptr<ID3D11ShaderResourceView> tex0,
                                     Com_ptr<ID3D11ShaderResourceView> tex1) noexcept
{
   if (tex0) _noise_tex0 = std::move(tex0);
   if (tex1) _noise_tex1 = std::move(tex1);
}

void Cloud_layer::apply(ID3D11DeviceContext4& dc, const Cloud_layer_input& input,
                        const core::cb::CloudLayers& constants, Profiler& profiler) noexcept
{
   if (!_enabled) return;
   if (!_noise_tex0 || !_noise_tex1) return;

   Profile profile{profiler, dc, "SWBF3 Cloud Layer"};

   // Update constant buffer
   D3D11_MAPPED_SUBRESOURCE mapped;
   if (SUCCEEDED(dc.Map(_constant_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      std::memcpy(mapped.pData, &constants, sizeof(constants));
      dc.Unmap(_constant_buffer.get(), 0);
   }

   // Clear state
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
   ID3D11ShaderResourceView* srvs[] = {&input.depth_srv, _noise_tex0.get(), _noise_tex1.get()};
   ID3D11SamplerState* samplers[] = {_wrap_sampler.get()};
   ID3D11Buffer* cbs[] = {_constant_buffer.get()};
   const float blend_factor[] = {0.0f, 0.0f, 0.0f, 0.0f};

   dc.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   dc.VSSetShader(_vs.get(), nullptr, 0);
   dc.RSSetViewports(1, &viewport);
   dc.PSSetShader(_ps.get(), nullptr, 0);
   dc.PSSetShaderResources(0, 3, srvs);
   dc.PSSetSamplers(0, 1, samplers);
   dc.PSSetConstantBuffers(0, 1, cbs);
   dc.OMSetBlendState(_blend_state.get(), blend_factor, 0xFFFFFFFF);
   dc.OMSetRenderTargets(1, &rtv, nullptr);

   // Draw fullscreen triangle
   dc.Draw(3, 0);
}

}

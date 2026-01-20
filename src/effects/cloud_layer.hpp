#pragma once

#include "../core/constant_buffers.hpp"
#include "../shader/database.hpp"
#include "com_ptr.hpp"
#include "profiler.hpp"

#include <d3d11_4.h>

#include <glm/glm.hpp>

namespace sp::effects {

struct Cloud_layer_input {
   ID3D11RenderTargetView& rtv;
   ID3D11ShaderResourceView& depth_srv;
   UINT width = 0;
   UINT height = 0;
};

class Cloud_layer {
public:
   Cloud_layer(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept;

   void apply(ID3D11DeviceContext4& dc, const Cloud_layer_input& input,
              const core::cb::CloudLayers& constants, Profiler& profiler) noexcept;

   // Enable/disable the entire cloud layer system
   void enabled(bool value) noexcept { _enabled = value; }
   bool enabled() const noexcept { return _enabled; }

   // Get mutable parameters for direct manipulation (all 3 layers)
   core::cb::CloudLayers& params() noexcept { return _params; }
   const core::cb::CloudLayers& params() const noexcept { return _params; }

   // Convenience accessors for individual layers
   core::cb::CloudLayerParams& layer(int index) noexcept { return _params.layers[index]; }
   const core::cb::CloudLayerParams& layer(int index) const noexcept { return _params.layers[index]; }

   // Override noise textures with custom ones
   void set_noise_textures(Com_ptr<ID3D11ShaderResourceView> tex0,
                           Com_ptr<ID3D11ShaderResourceView> tex1) noexcept;

private:
   void generate_noise_textures() noexcept;

   bool _enabled = false;
   core::cb::CloudLayers _params{};

   const Com_ptr<ID3D11Device5> _device;
   const Com_ptr<ID3D11VertexShader> _vs;
   const Com_ptr<ID3D11PixelShader> _ps;
   const Com_ptr<ID3D11Buffer> _constant_buffer;
   const Com_ptr<ID3D11SamplerState> _wrap_sampler;
   const Com_ptr<ID3D11BlendState> _blend_state;
   Com_ptr<ID3D11ShaderResourceView> _noise_tex0;
   Com_ptr<ID3D11ShaderResourceView> _noise_tex1;
};

}

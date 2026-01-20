#pragma once

#include "../core/constant_buffers.hpp"
#include "../shader/database.hpp"
#include "com_ptr.hpp"
#include "profiler.hpp"

#include <d3d11_4.h>
#include <array>
#include <vector>

#include <glm/glm.hpp>

namespace sp::effects {

// Maximum cloud volumes that can be rendered
constexpr int max_cloud_volumes = 128;

struct Cloud_volume_input {
   ID3D11RenderTargetView& rtv;
   ID3D11ShaderResourceView& depth_srv;
   UINT width = 0;
   UINT height = 0;
};

class Cloud_volume {
public:
   Cloud_volume(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept;

   void apply(ID3D11DeviceContext4& dc, const Cloud_volume_input& input,
              Profiler& profiler) noexcept;

   // Enable/disable the cloud volume system
   void enabled(bool value) noexcept { _enabled = value; }
   bool enabled() const noexcept { return _enabled; }

   // Get mutable parameters
   core::cb::CloudVolumes& params() noexcept { return _params; }
   const core::cb::CloudVolumes& params() const noexcept { return _params; }

   // Regenerate cloud positions based on current params
   void regenerate_clouds() noexcept;

   // Set runtime camera/projection data
   void set_view_projection(const glm::mat4& view, const glm::mat4& proj,
                            const glm::vec3& camera_pos, float time) noexcept;

private:
   void create_instance_buffer() noexcept;
   void generate_noise_texture() noexcept;

   bool _enabled = false;
   bool _needs_regeneration = true;
   core::cb::CloudVolumes _params{};

   // Runtime data set each frame
   glm::mat4 _view_matrix{1.0f};
   glm::mat4 _proj_matrix{1.0f};
   glm::vec3 _camera_position{0.0f};
   float _time{0.0f};

   // Generated cloud instances
   struct CloudInstance {
      glm::vec3 position;
      glm::vec3 size;
      float rotation;
      float noise_offset;
   };
   std::vector<CloudInstance> _instances;

   const Com_ptr<ID3D11Device5> _device;
   const Com_ptr<ID3D11VertexShader> _vs;
   const Com_ptr<ID3D11PixelShader> _ps;
   const Com_ptr<ID3D11Buffer> _constant_buffer;
   const Com_ptr<ID3D11Buffer> _instance_buffer;
   const Com_ptr<ID3D11SamplerState> _sampler;
   const Com_ptr<ID3D11BlendState> _blend_state;
   const Com_ptr<ID3D11RasterizerState> _raster_state;
   const Com_ptr<ID3D11DepthStencilState> _depth_state;
   Com_ptr<ID3D11ShaderResourceView> _noise_tex;
};

}

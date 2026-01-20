#pragma once

#include "../core/constant_buffers.hpp"
#include "../shader/database.hpp"
#include "com_ptr.hpp"
#include "profiler.hpp"

#include <d3d11_4.h>

#include <glm/glm.hpp>

namespace sp::effects {

enum class Fog_debug_mode {
   none,
   depth,
   world_y
};

struct Postprocess_fog_input {
   ID3D11RenderTargetView& rtv;
   ID3D11ShaderResourceView& scene_srv;
   ID3D11ShaderResourceView& depth_srv;
   UINT width = 0;
   UINT height = 0;
};

class Postprocess_fog {
public:
   Postprocess_fog(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept;

   void apply(ID3D11DeviceContext4& dc, const Postprocess_fog_input& input,
              const core::cb::Fog& fog_constants, Profiler& profiler) noexcept;

   // Enable/disable fog
   void enabled(bool value) noexcept { _enabled = value; }
   bool enabled() const noexcept { return _enabled; }

   // Update fog parameters (SWBF3 naming)
   void set_fog_color(const glm::vec4& color) noexcept { _fog_params.fog_color = color; }
   void set_fog_range(float start, float end) noexcept
   {
      _fog_params.fog_start = start;
      _fog_params.fog_end = end;
   }
   void set_height_fog(float base, float ceiling, float density, float alpha) noexcept
   {
      _fog_params.height_base = base;
      _fog_params.height_ceiling = ceiling;
      _fog_params.atmos_density = density;
      _fog_params.fog_alpha = alpha;
   }
   void set_height_falloff(float falloff) noexcept { _fog_params.height_falloff = falloff; }
   void set_ceiling_fade(float fade) noexcept { _fog_params.ceiling_fade = fade; }
   void set_fog_disc(float center_x, float center_z, float radius, float edge_fade) noexcept
   {
      _fog_params.fog_disc_center_x = center_x;
      _fog_params.fog_disc_center_z = center_z;
      _fog_params.fog_disc_radius = radius;
      _fog_params.fog_disc_edge_fade = edge_fade;
   }
   void set_blend_additive(bool additive) noexcept { _fog_params.blend_additive = additive ? 1 : 0; }
   void set_apply_to_sky(bool apply) noexcept { _fog_params.apply_to_sky = apply ? 1 : 0; }

   // Get mutable fog parameters for direct manipulation
   core::cb::Fog& fog_params() noexcept { return _fog_params; }
   const core::cb::Fog& fog_params() const noexcept { return _fog_params; }

   // Debug mode
   void debug_mode(Fog_debug_mode mode) noexcept { _debug_mode = mode; }
   Fog_debug_mode debug_mode() const noexcept { return _debug_mode; }

private:
   bool _enabled = false;
   Fog_debug_mode _debug_mode = Fog_debug_mode::none;
   core::cb::Fog _fog_params{};

   const Com_ptr<ID3D11Device5> _device;
   const Com_ptr<ID3D11VertexShader> _vs;
   const Com_ptr<ID3D11PixelShader> _ps;
   const Com_ptr<ID3D11PixelShader> _ps_debug_depth;
   const Com_ptr<ID3D11PixelShader> _ps_debug_world_y;
   const Com_ptr<ID3D11Buffer> _constant_buffer;
   const Com_ptr<ID3D11SamplerState> _point_sampler;
};

}

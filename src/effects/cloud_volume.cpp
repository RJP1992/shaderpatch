
#include "cloud_volume.hpp"
#include "../logger.hpp"

#include <cstring>
#include <random>

using namespace std::literals;

namespace sp::effects {

namespace {

struct alignas(16) Gpu_constants {
   glm::mat4 view_matrix;
   glm::mat4 proj_matrix;
   glm::mat4 view_proj_matrix;

   glm::vec3 camera_position;
   float time;

   glm::vec3 sun_direction;
   float sharpness;

   glm::vec3 light_color;
   float light_scattering;

   glm::vec3 dark_color;
   float max_lighting;

   float min_lighting;
   float noise_influence;
   float noise_tiling;
   float density;

   float depth_fade_near;
   float depth_fade_far;
   float edge_softness;
   float evolution_speed;

   glm::vec2 depth_linearize_params;
   glm::vec2 _padding;
};

static_assert(sizeof(Gpu_constants) == 304);

struct Gpu_instance {
   glm::vec3 position;
   float rotation;
   glm::vec3 size;
   float noise_offset;
};

static_assert(sizeof(Gpu_instance) == 32);

auto make_constant_buffer(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11Buffer>
{
   const D3D11_BUFFER_DESC desc{
      .ByteWidth = sizeof(Gpu_constants),
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

auto make_instance_buffer(ID3D11Device5& device, UINT max_instances) noexcept -> Com_ptr<ID3D11Buffer>
{
   const D3D11_BUFFER_DESC desc{
      .ByteWidth = sizeof(Gpu_instance) * max_instances,
      .Usage = D3D11_USAGE_DYNAMIC,
      .BindFlags = D3D11_BIND_SHADER_RESOURCE,
      .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
      .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
      .StructureByteStride = sizeof(Gpu_instance),
   };

   Com_ptr<ID3D11Buffer> buffer;
   device.CreateBuffer(&desc, nullptr, buffer.clear_and_assign());

   return buffer;
}

auto make_sampler(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11SamplerState>
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

auto make_blend_state(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11BlendState>
{
   D3D11_BLEND_DESC desc{};
   desc.AlphaToCoverageEnable = FALSE;
   desc.IndependentBlendEnable = FALSE;
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

auto make_raster_state(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11RasterizerState>
{
   D3D11_RASTERIZER_DESC desc{};
   desc.FillMode = D3D11_FILL_SOLID;
   desc.CullMode = D3D11_CULL_NONE;  // Double-sided billboards
   desc.FrontCounterClockwise = FALSE;
   desc.DepthBias = 0;
   desc.SlopeScaledDepthBias = 0.0f;
   desc.DepthBiasClamp = 0.0f;
   desc.DepthClipEnable = TRUE;
   desc.ScissorEnable = FALSE;
   desc.MultisampleEnable = FALSE;
   desc.AntialiasedLineEnable = FALSE;

   Com_ptr<ID3D11RasterizerState> state;
   device.CreateRasterizerState(&desc, state.clear_and_assign());

   return state;
}

auto make_depth_state(ID3D11Device5& device) noexcept -> Com_ptr<ID3D11DepthStencilState>
{
   D3D11_DEPTH_STENCIL_DESC desc{};
   desc.DepthEnable = TRUE;
   desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;  // Read-only depth
   desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
   desc.StencilEnable = FALSE;

   Com_ptr<ID3D11DepthStencilState> state;
   device.CreateDepthStencilState(&desc, state.clear_and_assign());

   return state;
}

}

Cloud_volume::Cloud_volume(Com_ptr<ID3D11Device5> device,
                           shader::Database& shaders) noexcept
   : _device{device},
     _vs{std::get<0>(shaders.vertex("cloud_volume"sv).entrypoint("main_vs"sv))},
     _ps{shaders.pixel("cloud_volume"sv).entrypoint("main_ps"sv)},
     _constant_buffer{make_constant_buffer(*device)},
     _instance_buffer{make_instance_buffer(*device, max_cloud_volumes)},
     _sampler{make_sampler(*device)},
     _blend_state{make_blend_state(*device)},
     _raster_state{make_raster_state(*device)},
     _depth_state{make_depth_state(*device)}
{
   generate_noise_texture();
}

void Cloud_volume::apply(ID3D11DeviceContext4& dc, const Cloud_volume_input& input,
                         Profiler& profiler) noexcept
{
   if (!_enabled) return;

   // Regenerate clouds if needed (must happen before empty check)
   if (_needs_regeneration) {
      regenerate_clouds();
      _needs_regeneration = false;
   }

   if (_instances.empty()) return;

   Profile profile{profiler, dc, "Cloud Volumes"};

   // Update instance buffer
   {
      D3D11_MAPPED_SUBRESOURCE mapped;
      if (SUCCEEDED(dc.Map(_instance_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
         auto* gpu_instances = static_cast<Gpu_instance*>(mapped.pData);

         for (size_t i = 0; i < _instances.size() && i < max_cloud_volumes; ++i) {
            gpu_instances[i].position = _instances[i].position;
            gpu_instances[i].rotation = _instances[i].rotation;
            gpu_instances[i].size = _instances[i].size;
            gpu_instances[i].noise_offset = _instances[i].noise_offset;
         }

         dc.Unmap(_instance_buffer.get(), 0);
      }
   }

   // Update constant buffer
   {
      Gpu_constants constants;

      constants.view_matrix = glm::transpose(_view_matrix);
      constants.proj_matrix = glm::transpose(_proj_matrix);
      constants.view_proj_matrix = glm::transpose(_proj_matrix * _view_matrix);

      constants.camera_position = _camera_position;
      constants.time = _time;

      constants.sun_direction = glm::normalize(_params.sun_direction);
      constants.sharpness = _params.sharpness;

      constants.light_color = _params.light_color;
      constants.light_scattering = _params.light_scattering;

      constants.dark_color = _params.dark_color;
      constants.max_lighting = _params.max_lighting;

      constants.min_lighting = _params.min_lighting;
      constants.noise_influence = _params.noise_influence;
      constants.noise_tiling = _params.noise_tiling;
      constants.density = _params.density;

      constants.depth_fade_near = _params.depth_fade_near;
      constants.depth_fade_far = _params.depth_fade_far;
      constants.edge_softness = _params.edge_softness;
      constants.evolution_speed = _params.evolution_speed;

      // Calculate depth linearization from projection matrix
      float linearize_mul = -_proj_matrix[3][2];
      float linearize_add = _proj_matrix[2][2];
      if (linearize_mul * linearize_add < 0) {
         linearize_add = -linearize_add;
      }
      constants.depth_linearize_params = glm::vec2(linearize_mul, linearize_add);

      D3D11_MAPPED_SUBRESOURCE mapped;
      if (SUCCEEDED(dc.Map(_constant_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
         std::memcpy(mapped.pData, &constants, sizeof(constants));
         dc.Unmap(_constant_buffer.get(), 0);
      }
   }

   // Create SRV for instance buffer
   Com_ptr<ID3D11ShaderResourceView> instance_srv;
   {
      D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
      desc.Format = DXGI_FORMAT_UNKNOWN;
      desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
      desc.Buffer.FirstElement = 0;
      desc.Buffer.NumElements = static_cast<UINT>(_instances.size());

      _device->CreateShaderResourceView(_instance_buffer.get(), &desc,
                                        instance_srv.clear_and_assign());
   }

   // Set up pipeline state
   dc.VSSetShader(_vs.get(), nullptr, 0);
   dc.PSSetShader(_ps.get(), nullptr, 0);

   ID3D11Buffer* vs_cbs[] = {_constant_buffer.get()};
   dc.VSSetConstantBuffers(0, 1, vs_cbs);

   ID3D11Buffer* ps_cbs[] = {_constant_buffer.get()};
   dc.PSSetConstantBuffers(0, 1, ps_cbs);

   ID3D11ShaderResourceView* vs_srvs[] = {nullptr, nullptr, instance_srv.get()};
   dc.VSSetShaderResources(0, 3, vs_srvs);

   ID3D11ShaderResourceView* ps_srvs[] = {&input.depth_srv, _noise_tex.get()};
   dc.PSSetShaderResources(0, 2, ps_srvs);

   ID3D11SamplerState* samplers[] = {_sampler.get()};
   dc.PSSetSamplers(0, 1, samplers);

   dc.OMSetBlendState(_blend_state.get(), nullptr, 0xFFFFFFFF);
   dc.RSSetState(_raster_state.get());
   dc.OMSetDepthStencilState(_depth_state.get(), 0);

   auto* rtv = &input.rtv;
   dc.OMSetRenderTargets(1, &rtv, nullptr);

   D3D11_VIEWPORT viewport{};
   viewport.TopLeftX = 0.0f;
   viewport.TopLeftY = 0.0f;
   viewport.Width = static_cast<float>(input.width);
   viewport.Height = static_cast<float>(input.height);
   viewport.MinDepth = 0.0f;
   viewport.MaxDepth = 1.0f;
   dc.RSSetViewports(1, &viewport);

   dc.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   dc.IASetInputLayout(nullptr);
   dc.IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
   dc.IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

   // Draw instanced quads
   dc.DrawInstanced(4, static_cast<UINT>(_instances.size()), 0, 0);

   // Clean up
   ID3D11ShaderResourceView* null_srvs[3] = {nullptr, nullptr, nullptr};
   dc.VSSetShaderResources(0, 3, null_srvs);
   dc.PSSetShaderResources(0, 2, null_srvs);
}

void Cloud_volume::set_view_projection(const glm::mat4& view, const glm::mat4& proj,
                                       const glm::vec3& camera_pos, float time) noexcept
{
   _view_matrix = view;
   _proj_matrix = proj;
   _camera_position = camera_pos;
   _time = time;
}

void Cloud_volume::regenerate_clouds() noexcept
{
   _instances.clear();

   if (_params.cloud_count == 0) return;

   std::mt19937 rng{_params.seed};
   std::uniform_real_distribution<float> dist_x(_params.area_min.x, _params.area_max.x);
   std::uniform_real_distribution<float> dist_y(_params.area_min.y, _params.area_max.y);
   std::uniform_real_distribution<float> dist_z(_params.area_min.z, _params.area_max.z);
   std::uniform_real_distribution<float> dist_size_x(_params.cloud_size_min.x, _params.cloud_size_max.x);
   std::uniform_real_distribution<float> dist_size_y(_params.cloud_size_min.y, _params.cloud_size_max.y);
   std::uniform_real_distribution<float> dist_size_z(_params.cloud_size_min.z, _params.cloud_size_max.z);
   std::uniform_real_distribution<float> dist_rotation(0.0f, 6.283185f);
   std::uniform_real_distribution<float> dist_noise(0.0f, 1.0f);

   _instances.reserve(_params.cloud_count);

   for (std::uint32_t i = 0; i < _params.cloud_count && i < max_cloud_volumes; ++i) {
      CloudInstance inst;
      inst.position = glm::vec3(dist_x(rng), dist_y(rng), dist_z(rng));
      inst.size = glm::vec3(dist_size_x(rng), dist_size_y(rng), dist_size_z(rng));
      inst.rotation = dist_rotation(rng);
      inst.noise_offset = dist_noise(rng);

      _instances.push_back(inst);
   }

   // Sort by distance from camera (back to front) for proper blending
   // This will be done each frame based on camera position
}

void Cloud_volume::generate_noise_texture() noexcept
{
   constexpr int size = 64;

   std::array<uint8_t, size * size * size> pixels;

   // Generate 3D Perlin-like noise
   std::mt19937 rng{42};
   std::uniform_real_distribution<float> dist(0.0f, 1.0f);

   // Simple 3D value noise with interpolation
   auto hash = [](int x, int y, int z) {
      int n = x + y * 57 + z * 113;
      n = (n << 13) ^ n;
      return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f) * 0.5f + 0.5f;
   };

   auto lerp = [](float a, float b, float t) { return a + t * (b - a); };
   auto smoothstep = [](float t) { return t * t * (3.0f - 2.0f * t); };

   auto noise3d = [&](float x, float y, float z) {
      int xi = static_cast<int>(std::floor(x));
      int yi = static_cast<int>(std::floor(y));
      int zi = static_cast<int>(std::floor(z));

      float xf = x - xi;
      float yf = y - yi;
      float zf = z - zi;

      float u = smoothstep(xf);
      float v = smoothstep(yf);
      float w = smoothstep(zf);

      float c000 = hash(xi, yi, zi);
      float c100 = hash(xi + 1, yi, zi);
      float c010 = hash(xi, yi + 1, zi);
      float c110 = hash(xi + 1, yi + 1, zi);
      float c001 = hash(xi, yi, zi + 1);
      float c101 = hash(xi + 1, yi, zi + 1);
      float c011 = hash(xi, yi + 1, zi + 1);
      float c111 = hash(xi + 1, yi + 1, zi + 1);

      float x00 = lerp(c000, c100, u);
      float x10 = lerp(c010, c110, u);
      float x01 = lerp(c001, c101, u);
      float x11 = lerp(c011, c111, u);

      float y0 = lerp(x00, x10, v);
      float y1 = lerp(x01, x11, v);

      return lerp(y0, y1, w);
   };

   for (int z = 0; z < size; ++z) {
      for (int y = 0; y < size; ++y) {
         for (int x = 0; x < size; ++x) {
            float fx = static_cast<float>(x) / size;
            float fy = static_cast<float>(y) / size;
            float fz = static_cast<float>(z) / size;

            // Fractal noise with multiple octaves
            float value = 0.0f;
            float amplitude = 1.0f;
            float frequency = 4.0f;

            for (int oct = 0; oct < 4; ++oct) {
               value += noise3d(fx * frequency, fy * frequency, fz * frequency) * amplitude;
               amplitude *= 0.5f;
               frequency *= 2.0f;
            }

            value = std::clamp(value, 0.0f, 1.0f);
            pixels[z * size * size + y * size + x] = static_cast<uint8_t>(value * 255.0f);
         }
      }
   }

   // Create 3D texture
   D3D11_TEXTURE3D_DESC tex_desc{};
   tex_desc.Width = size;
   tex_desc.Height = size;
   tex_desc.Depth = size;
   tex_desc.MipLevels = 1;
   tex_desc.Format = DXGI_FORMAT_R8_UNORM;
   tex_desc.Usage = D3D11_USAGE_IMMUTABLE;
   tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

   D3D11_SUBRESOURCE_DATA init_data{};
   init_data.pSysMem = pixels.data();
   init_data.SysMemPitch = size;
   init_data.SysMemSlicePitch = size * size;

   Com_ptr<ID3D11Texture3D> texture;
   _device->CreateTexture3D(&tex_desc, &init_data, texture.clear_and_assign());

   if (texture) {
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
      srv_desc.Format = DXGI_FORMAT_R8_UNORM;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
      srv_desc.Texture3D.MostDetailedMip = 0;
      srv_desc.Texture3D.MipLevels = 1;

      _device->CreateShaderResourceView(texture.get(), &srv_desc, _noise_tex.clear_and_assign());
   }
}

}

// ===========================================================================
// CLOUDS.CPP - Billboard Particle Clouds v13
// ===========================================================================

#include "clouds.hpp"
#include "../core/d3d11_helpers.hpp"
#include "../user_config.hpp"
#include "com_ptr.hpp"

#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <random>

#include <d3d11_1.h>

namespace sp::effects {

    using namespace std::literals;

    namespace {

        constexpr UINT cloud_texture_size = 128;
        constexpr UINT max_particles = 10000;

        // Particle data matching shader struct
        struct Particle {
            glm::vec3 position;
            float size;
            glm::vec3 color;
            float alpha;
            float rotation;
            glm::vec3 _pad;
        };

        static_assert(sizeof(Particle) == 48);

        // Generate soft cloud texture
        auto create_cloud_texture(ID3D11Device5& device) -> Com_ptr<ID3D11ShaderResourceView>
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = cloud_texture_size;
            desc.Height = cloud_texture_size;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            std::vector<uint32_t> pixels(cloud_texture_size * cloud_texture_size);

            // Random noise for variation
            std::mt19937 rng(12345);
            std::uniform_real_distribution<float> noise_dist(-0.1f, 0.1f);

            float center = cloud_texture_size * 0.5f;
            float max_dist = center * 0.9f;

            for (UINT y = 0; y < cloud_texture_size; ++y) {
                for (UINT x = 0; x < cloud_texture_size; ++x) {
                    float dx = (float(x) - center) / max_dist;
                    float dy = (float(y) - center) / max_dist;
                    float dist_sq = dx * dx + dy * dy;

                    // Soft falloff with noise
                    float alpha = 1.0f - std::sqrt(dist_sq);
                    alpha = std::max(0.0f, alpha);

                    // Apply power curve for softer edges
                    alpha = alpha * alpha * alpha;

                    // Add some noise for organic feel
                    alpha += noise_dist(rng) * alpha;
                    alpha = std::clamp(alpha, 0.0f, 1.0f);

                    // Slightly brighter in center
                    float brightness = 0.9f + 0.1f * (1.0f - dist_sq);
                    brightness = std::clamp(brightness, 0.0f, 1.0f);

                    uint8_t r = static_cast<uint8_t>(brightness * 255);
                    uint8_t g = static_cast<uint8_t>(brightness * 255);
                    uint8_t b = static_cast<uint8_t>(brightness * 255);
                    uint8_t a = static_cast<uint8_t>(alpha * 255);

                    pixels[y * cloud_texture_size + x] = (a << 24) | (b << 16) | (g << 8) | r;
                }
            }

            D3D11_SUBRESOURCE_DATA init_data{};
            init_data.pSysMem = pixels.data();
            init_data.SysMemPitch = cloud_texture_size * sizeof(uint32_t);

            Com_ptr<ID3D11Texture2D> texture;
            if (FAILED(device.CreateTexture2D(&desc, &init_data, texture.clear_and_assign()))) {
                return nullptr;
            }

            Com_ptr<ID3D11ShaderResourceView> srv;
            device.CreateShaderResourceView(texture.get(), nullptr, srv.clear_and_assign());
            return srv;
        }

        bool point_inside_ellipsoid(const glm::vec3& point, const glm::vec3& center, const glm::vec3& radii)
        {
            glm::vec3 local = (point - center) / radii;
            return glm::dot(local, local) < 1.0f;
        }

    } // anonymous namespace

    // Constant buffer matching shader
    struct Cloud_constants {
        glm::mat4 view_matrix;          // 64
        glm::mat4 proj_matrix;          // 64 = 128
        glm::mat4 view_proj;            // 64 = 192

        glm::vec3 camera_position;      // 12
        float time;                     // 4 = 208

        glm::vec3 camera_right;         // 12
        float cloud_alpha;              // 4 = 224

        glm::vec3 camera_up;            // 12
        float fade_near;                // 4 = 240

        glm::vec3 light_color;          // 12
        float fade_far;                 // 4 = 256

        glm::vec3 dark_color;           // 12
        float depth_cutoff_dist;        // 4 = 272

        glm::vec2 depth_params;         // 8
        glm::vec2 screen_size;          // 8 = 288

        glm::vec3 wind_velocity;        // 12
        float _pad;                     // 4 = 304
    };

    static_assert(sizeof(Cloud_constants) == 304);

    class Clouds::Impl {
    public:
        Impl(Com_ptr<ID3D11Device5> device, shader::Database& shaders)
            : _device{ std::move(device) },
            _cloud_ps{ shaders.pixel("clouds"sv).entrypoint("clouds_ps"sv) },
            _fog_ps{ shaders.pixel("clouds"sv).entrypoint("cloud_fog_ps"sv) },
            _cloud_texture{ create_cloud_texture(*_device) },
            _constant_buffer{ core::create_dynamic_constant_buffer(*_device, sizeof(Cloud_constants)) }
        {
            auto [vs, bytecode, layout] = shaders.vertex("clouds"sv).entrypoint("cloud_vs"sv);
            _cloud_vs = std::move(vs);

            auto [fog_vs, fog_bytecode, fog_layout] = shaders.vertex("clouds"sv).entrypoint("cloud_fog_vs"sv);
            _fog_vs = std::move(fog_vs);

            // Create particle buffer
            D3D11_BUFFER_DESC buf_desc{};
            buf_desc.ByteWidth = max_particles * sizeof(Particle);
            buf_desc.Usage = D3D11_USAGE_DYNAMIC;
            buf_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            buf_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            buf_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            buf_desc.StructureByteStride = sizeof(Particle);
            _device->CreateBuffer(&buf_desc, nullptr, _particle_buffer.clear_and_assign());

            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
            srv_desc.Format = DXGI_FORMAT_UNKNOWN;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srv_desc.Buffer.FirstElement = 0;
            srv_desc.Buffer.NumElements = max_particles;
            _device->CreateShaderResourceView(_particle_buffer.get(), &srv_desc,
                _particle_srv.clear_and_assign());

            // Samplers
            D3D11_SAMPLER_DESC sampler_desc{};
            sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
            sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
            sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
            _device->CreateSamplerState(&sampler_desc, _linear_wrap_sampler.clear_and_assign());

            sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            _device->CreateSamplerState(&sampler_desc, _point_clamp_sampler.clear_and_assign());

            sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            _device->CreateSamplerState(&sampler_desc, _linear_clamp_sampler.clear_and_assign());

            // Blend state - standard alpha
            CD3D11_BLEND_DESC blend_desc{ CD3D11_DEFAULT{} };
            blend_desc.RenderTarget[0].BlendEnable = TRUE;
            blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            _device->CreateBlendState(&blend_desc, _blend_state.clear_and_assign());

            D3D11_DEPTH_STENCIL_DESC ds_desc{};
            ds_desc.DepthEnable = FALSE;
            ds_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            _device->CreateDepthStencilState(&ds_desc, _depth_stencil_state.clear_and_assign());

            D3D11_RASTERIZER_DESC rs_desc{};
            rs_desc.FillMode = D3D11_FILL_SOLID;
            rs_desc.CullMode = D3D11_CULL_NONE;  // Billboards face camera, no culling
            rs_desc.DepthClipEnable = TRUE;
            _device->CreateRasterizerState(&rs_desc, _rasterizer_state.clear_and_assign());
        }

        void params(const Cloud_params& params) noexcept
        {
            // Mark dirty if anything relevant changed
            bool volumes_changed = params.volumes.size() != _params.volumes.size();
            bool settings_changed = params.particles_per_volume != _params.particles_per_volume ||
                std::abs(params.particle_size - _params.particle_size) > 0.1f;

            // Also check if any volume positions/scales changed
            if (!volumes_changed && !settings_changed) {
                for (size_t i = 0; i < params.volumes.size() && i < _params.volumes.size(); ++i) {
                    if (glm::distance(params.volumes[i].position, _params.volumes[i].position) > 0.1f ||
                        std::abs(params.volumes[i].radius - _params.volumes[i].radius) > 0.1f) {
                        volumes_changed = true;
                        break;
                    }
                }
            }

            if (volumes_changed || settings_changed) {
                _particles_dirty = true;
            }
            _params = params;
        }

        auto params() const noexcept -> const Cloud_params& { return _params; }

        void rebuild_particles()
        {
            _particles.clear();
            _particles.reserve(max_particles);

            std::mt19937 rng(42);
            std::uniform_real_distribution<float> dist_01(0.0f, 1.0f);
            std::uniform_real_distribution<float> dist_angle(0.0f, 6.28318f);
            std::uniform_real_distribution<float> dist_signed(-1.0f, 1.0f);

            for (const auto& vol : _params.volumes) {
                // Particles per volume based on size
                int particle_count = static_cast<int>(_params.particles_per_volume);

                for (int i = 0; i < particle_count && _particles.size() < max_particles; ++i) {
                    Particle p;

                    // Random position within ellipsoid
                    float u = dist_01(rng);
                    float v = dist_01(rng);
                    float theta = 2.0f * 3.14159f * u;
                    float phi = std::acos(2.0f * v - 1.0f);
                    float r = std::cbrt(dist_01(rng));  // Cube root for uniform volume distribution

                    float x = r * std::sin(phi) * std::cos(theta);
                    float y = r * std::sin(phi) * std::sin(theta);
                    float z = r * std::cos(phi);

                    p.position = vol.position + glm::vec3(x, z, y) * vol.scale * vol.radius;

                    // Size variation
                    p.size = _params.particle_size * (0.5f + dist_01(rng) * 1.0f);

                    // Color variation (slight)
                    float brightness = 0.85f + dist_01(rng) * 0.15f;
                    p.color = glm::mix(_params.dark_color, _params.light_color, brightness);

                    // Alpha variation
                    p.alpha = vol.density * (0.6f + dist_01(rng) * 0.4f);

                    // Random rotation
                    p.rotation = dist_angle(rng);

                    _particles.push_back(p);
                }
            }

            _particles_dirty = false;
        }

        void render(ID3D11DeviceContext1& dc,
            ID3D11RenderTargetView& output_rtv,
            const Clouds_input& input,
            Profiler& profiler) noexcept
        {
            if (!_params.enabled || _params.volumes.empty()) return;

            Profile profile{ profiler, dc, "Volumetric Clouds"sv };

            // Rebuild particles if needed
            if (_particles_dirty) {
                rebuild_particles();
            }

            if (_particles.empty()) return;

            // Update particle buffer
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(dc.Map(_particle_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                memcpy(mapped.pData, _particles.data(), _particles.size() * sizeof(Particle));
                dc.Unmap(_particle_buffer.get(), 0);
            }

            // Extract camera vectors from view matrix
            glm::mat4 inv_view = glm::inverse(input.view_matrix);
            glm::vec3 camera_right = glm::vec3(inv_view[0]);
            glm::vec3 camera_up = glm::vec3(inv_view[1]);

            const glm::mat4 view_proj = input.projection_matrix * input.view_matrix;
            const glm::vec2 depth_params = { input.projection_matrix[2][2], input.projection_matrix[3][2] };

            // Check if inside any volume
            const Cloud_volume* inside_volume = nullptr;
            for (const auto& vol : _params.volumes) {
                if (point_inside_ellipsoid(input.camera_position, vol.position, vol.scale * vol.radius)) {
                    inside_volume = &vol;
                    break;
                }
            }

            // Setup render state
            ID3D11ShaderResourceView* srvs[] = { &input.depth_srv, _cloud_texture.get(), _particle_srv.get() };
            dc.VSSetShaderResources(0, 3, srvs);
            dc.PSSetShaderResources(0, 3, srvs);

            ID3D11SamplerState* samplers[] = { _linear_wrap_sampler.get(), _point_clamp_sampler.get(), _linear_clamp_sampler.get() };
            dc.PSSetSamplers(0, 3, samplers);

            ID3D11Buffer* cbs[] = { _constant_buffer.get() };
            dc.VSSetConstantBuffers(1, 1, cbs);
            dc.PSSetConstantBuffers(1, 1, cbs);

            ID3D11RenderTargetView* rtvs[] = { &output_rtv };
            dc.OMSetRenderTargets(1, rtvs, nullptr);
            dc.OMSetBlendState(_blend_state.get(), nullptr, 0xffffffff);
            dc.OMSetDepthStencilState(_depth_stencil_state.get(), 0);
            dc.RSSetState(_rasterizer_state.get());

            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(input.width);
            viewport.Height = static_cast<float>(input.height);
            viewport.MaxDepth = 1.0f;
            dc.RSSetViewports(1, &viewport);

            // Fill constant buffer
            Cloud_constants constants{};
            constants.view_matrix = input.view_matrix;
            constants.proj_matrix = input.projection_matrix;
            constants.view_proj = view_proj;
            constants.camera_position = input.camera_position;
            constants.time = input.time;
            constants.camera_right = camera_right;
            constants.cloud_alpha = _params.cloud_alpha;
            constants.camera_up = camera_up;
            constants.fade_near = _params.fade_near;
            constants.light_color = _params.light_color;
            constants.fade_far = _params.fade_far;
            constants.dark_color = _params.dark_color;
            constants.depth_cutoff_dist = _params.depth_cutoff_distance;
            constants.depth_params = depth_params;
            constants.screen_size = { input.width, input.height };
            constants.wind_velocity = glm::vec3(_params.wind_direction.x, 0.0f, _params.wind_direction.y) * _params.wind_speed;

            core::update_dynamic_buffer(dc, *_constant_buffer, constants);

            // Fog pass if inside a cloud
            if (inside_volume) {
                dc.VSSetShader(_fog_vs.get(), nullptr, 0);
                dc.PSSetShader(_fog_ps.get(), nullptr, 0);

                dc.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                dc.IASetInputLayout(nullptr);
                dc.Draw(3, 0);
            }

            // Render cloud particles - instanced
            dc.VSSetShader(_cloud_vs.get(), nullptr, 0);
            dc.PSSetShader(_cloud_ps.get(), nullptr, 0);

            dc.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            dc.IASetInputLayout(nullptr);  // No input layout, using SV_VertexID

            // 6 vertices per quad, N instances
            dc.DrawInstanced(6, static_cast<UINT>(_particles.size()), 0, 0);

            // Cleanup
            ID3D11ShaderResourceView* null_srvs[] = { nullptr, nullptr, nullptr };
            dc.VSSetShaderResources(0, 3, null_srvs);
            dc.PSSetShaderResources(0, 3, null_srvs);
            dc.OMSetBlendState(nullptr, nullptr, 0xffffffff);
            dc.OMSetDepthStencilState(nullptr, 0);
            dc.RSSetState(nullptr);
        }

        void mark_dirty() { _particles_dirty = true; }

    private:
        Com_ptr<ID3D11Device5> _device;
        Com_ptr<ID3D11PixelShader> _cloud_ps;
        Com_ptr<ID3D11PixelShader> _fog_ps;
        Com_ptr<ID3D11VertexShader> _cloud_vs;
        Com_ptr<ID3D11VertexShader> _fog_vs;
        Com_ptr<ID3D11Buffer> _particle_buffer;
        Com_ptr<ID3D11ShaderResourceView> _particle_srv;
        Com_ptr<ID3D11ShaderResourceView> _cloud_texture;
        Com_ptr<ID3D11Buffer> _constant_buffer;
        Com_ptr<ID3D11SamplerState> _linear_wrap_sampler;
        Com_ptr<ID3D11SamplerState> _point_clamp_sampler;
        Com_ptr<ID3D11SamplerState> _linear_clamp_sampler;
        Com_ptr<ID3D11BlendState> _blend_state;
        Com_ptr<ID3D11DepthStencilState> _depth_stencil_state;
        Com_ptr<ID3D11RasterizerState> _rasterizer_state;

        Cloud_params _params{};
        std::vector<Particle> _particles;
        bool _particles_dirty = true;
    };

    Clouds::Clouds(Com_ptr<ID3D11Device5> device, shader::Database& shaders)
        : _impl{ std::make_unique<Impl>(std::move(device), shaders) }
    {
    }

    Clouds::~Clouds() = default;

    void Clouds::params(const Cloud_params& params) noexcept { _impl->params(params); }
    auto Clouds::params() const noexcept -> const Cloud_params& { return _impl->params(); }

    void Clouds::render(ID3D11DeviceContext1& dc,
        ID3D11RenderTargetView& output_rtv,
        const Clouds_input& input,
        Profiler& profiler) noexcept
    {
        _impl->render(dc, output_rtv, input, profiler);
    }

} // namespace sp::effects

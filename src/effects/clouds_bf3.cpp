// ===========================================================================
// CLOUDS_BF3.CPP - BF3-Style Cloud Rendering Implementation
// ===========================================================================
// Renders clouds to a low-resolution buffer then upsamples with a depth-aware
// bilateral filter to avoid edge bleeding. This bypasses OIT entirely.
// ===========================================================================

#include "clouds_bf3.hpp"
#include "clouds_bf3_params.hpp"
#include "rendertarget_allocator.hpp"
#include "../core/d3d11_helpers.hpp"
#include "../core/texture_database.hpp"
#include "../shader/database.hpp"
#include "com_ptr.hpp"
#include "profiler.hpp"

#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <random>

#include <d3d11_1.h>

namespace sp::effects {

using namespace std::literals;

namespace {

    constexpr UINT max_particles = 80000;
    constexpr UINT particle_texture_size = 64;

    // Must match shader struct exactly
    struct alignas(16) GPU_Particle {
        glm::vec3 position;
        float size;
        glm::vec3 color;
        float alpha;
        float rotation;
        float noise_offset;
        float density;
        float _pad;
    };

    static_assert(sizeof(GPU_Particle) == 48, "GPU_Particle size mismatch");

    // Must match shader cbuffer exactly (b1)
    struct alignas(16) GPU_CloudConstants {
        glm::mat4 view_matrix;          // 0
        glm::mat4 proj_matrix;          // 64
        glm::mat4 view_proj_matrix;     // 128
        
        glm::vec3 camera_position;      // 192
        float cloud_time;               // 204
        
        glm::vec3 camera_right;         // 208
        float global_alpha;             // 220
        
        glm::vec3 camera_up;            // 224
        float fade_near;                // 236
        
        glm::vec3 camera_forward;       // 240
        float fade_far;                 // 252
        
        glm::vec2 screen_size;          // 256
        glm::vec2 depth_params;         // 264
        
        glm::vec3 sun_direction;        // 272
        float sun_intensity;            // 284
        
        glm::vec3 sun_color;            // 288
        float ambient_intensity;        // 300
        
        glm::vec3 cloud_color_bright;   // 304
        float _pad0;                    // 316
        
        glm::vec3 cloud_color_dark;     // 320
        float _pad1;                    // 332
        
        glm::vec3 cloud_color_ambient;  // 336
        float _pad2;                    // 348
        
        glm::vec4 octave_weights;       // 352
        
        float noise_scale;              // 368
        float noise_erosion;            // 372
        float noise_edge_sharpness;     // 376
        float noise_animation_speed;    // 380
        
        float scatter_forward;          // 384
        float scatter_forward_exp;      // 388
        float scatter_back;             // 392
        float absorption;               // 396
        
        float powder_strength;          // 400
        float ambient_boost;            // 404
        float self_shadow;              // 408
        float depth_softness;           // 412
    };

    static_assert(sizeof(GPU_CloudConstants) == 416, "GPU_CloudConstants size mismatch");

    // Upsample constant buffer (b2)
    struct alignas(16) GPU_UpsampleConstants {
        glm::vec2 lowres_size;
        glm::vec2 fullres_size;
        glm::vec2 texel_size;
        float depth_threshold;
        float upsample_sharpness;
        glm::vec2 upsample_depth_params;
        float use_firstperson_depth;
        float _pad;
    };

    static_assert(sizeof(GPU_UpsampleConstants) == 48, "GPU_UpsampleConstants size mismatch");

    // Simple hash function for procedural noise
    inline float hash(float n) {
        return std::fmod(std::sin(n) * 43758.5453123f, 1.0f);
    }
    
    // 2D noise function
    inline float noise2d(float x, float y) {
        float ix = std::floor(x);
        float iy = std::floor(y);
        float fx = x - ix;
        float fy = y - iy;
        
        // Smoothstep
        fx = fx * fx * (3.0f - 2.0f * fx);
        fy = fy * fy * (3.0f - 2.0f * fy);
        
        float a = hash(ix + iy * 57.0f);
        float b = hash(ix + 1.0f + iy * 57.0f);
        float c = hash(ix + (iy + 1.0f) * 57.0f);
        float d = hash(ix + 1.0f + (iy + 1.0f) * 57.0f);
        
        return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
    }
    
    // Fractal Brownian Motion
    inline float fbm(float x, float y, int octaves) {
        float value = 0.0f;
        float amplitude = 0.5f;
        float frequency = 1.0f;
        
        for (int i = 0; i < octaves; ++i) {
            value += amplitude * noise2d(x * frequency, y * frequency);
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        return value;
    }

    // Generate a soft cloud particle texture - NO HARD EDGES
    auto create_particle_texture(ID3D11Device5& device) -> Com_ptr<ID3D11ShaderResourceView>
    {
        constexpr UINT tex_size = 128;
        
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = tex_size;
        desc.Height = tex_size;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        std::vector<uint32_t> pixels(tex_size * tex_size);

        const float center = tex_size * 0.5f;

        for (UINT y = 0; y < tex_size; ++y) {
            for (UINT x = 0; x < tex_size; ++x) {
                float dx = (static_cast<float>(x) - center) / center;
                float dy = (static_cast<float>(y) - center) / center;
                float dist = std::sqrt(dx * dx + dy * dy);
                
                // Very soft gaussian-like falloff - NO hard edges
                // This creates a smooth gradient from center to edge
                float alpha = std::exp(-dist * dist * 2.5f);
                
                // Add subtle density variation (not edge variation)
                float density_var = fbm(x * 0.06f, y * 0.06f, 3) * 0.15f + 0.85f;
                alpha *= density_var;
                
                // Ensure very soft fade to zero at edges
                alpha *= (1.0f - dist * dist); // Additional soft falloff
                alpha = std::max(0.0f, alpha);
                
                // Brightness variation for internal detail
                float brightness = 0.85f + fbm(x * 0.08f, y * 0.08f, 2) * 0.15f;

                uint8_t r = static_cast<uint8_t>(std::min(1.0f, brightness) * 255);
                uint8_t g = static_cast<uint8_t>(std::min(1.0f, brightness) * 255);
                uint8_t b = static_cast<uint8_t>(std::min(1.0f, brightness) * 255);
                uint8_t a = static_cast<uint8_t>(std::min(1.0f, alpha) * 255);

                pixels[y * tex_size + x] = (a << 24) | (b << 16) | (g << 8) | r;
            }
        }

        D3D11_SUBRESOURCE_DATA init_data{};
        init_data.pSysMem = pixels.data();
        init_data.SysMemPitch = tex_size * sizeof(uint32_t);

        Com_ptr<ID3D11Texture2D> texture;
        if (FAILED(device.CreateTexture2D(&desc, &init_data, texture.clear_and_assign()))) {
            return nullptr;
        }

        Com_ptr<ID3D11ShaderResourceView> srv;
        device.CreateShaderResourceView(texture.get(), nullptr, srv.clear_and_assign());
        return srv;
    }

} // anonymous namespace

// ===========================================================================
// IMPLEMENTATION CLASS
// ===========================================================================

class Clouds_bf3::Impl {
public:
    Impl(Com_ptr<ID3D11Device5> device, shader::Database& shaders)
        : _device{std::move(device)}
    {
        // Load shaders
        auto [vs, vs_bytecode, vs_layout] = shaders.vertex("clouds_bf3"sv).entrypoint("cloud_vs"sv);
        _vertex_shader = std::move(vs);
        _pixel_shader = shaders.pixel("clouds_bf3"sv).entrypoint("cloud_ps"sv);

        auto [fog_vs, fog_bytecode, fog_layout] = shaders.vertex("clouds_bf3"sv).entrypoint("cloud_fog_vs"sv);
        _fog_vertex_shader = std::move(fog_vs);
        _fog_pixel_shader = shaders.pixel("clouds_bf3"sv).entrypoint("cloud_fog_ps"sv);

        // Upsample shaders
        auto [up_vs, up_bytecode, up_layout] = shaders.vertex("clouds_bf3"sv).entrypoint("cloud_upsample_vs"sv);
        _upsample_vertex_shader = std::move(up_vs);
        _upsample_pixel_shader = shaders.pixel("clouds_bf3"sv).entrypoint("cloud_upsample_ps"sv);
        _upsample_oit_pixel_shader = shaders.pixel("clouds_bf3"sv).entrypoint("cloud_upsample_oit_ps"sv);

        // Create particle texture
        _particle_texture = create_particle_texture(*_device);

        // Create particle buffer (structured buffer for shader access)
        D3D11_BUFFER_DESC buf_desc{};
        buf_desc.ByteWidth = max_particles * sizeof(GPU_Particle);
        buf_desc.Usage = D3D11_USAGE_DYNAMIC;
        buf_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        buf_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        buf_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        buf_desc.StructureByteStride = sizeof(GPU_Particle);
        _device->CreateBuffer(&buf_desc, nullptr, _particle_buffer.clear_and_assign());

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.NumElements = max_particles;
        _device->CreateShaderResourceView(_particle_buffer.get(), &srv_desc, 
            _particle_buffer_srv.clear_and_assign());

        // Create constant buffers
        _constant_buffer = core::create_dynamic_constant_buffer(*_device, sizeof(GPU_CloudConstants));
        _upsample_constant_buffer = core::create_dynamic_constant_buffer(*_device, sizeof(GPU_UpsampleConstants));

        // Create samplers
        D3D11_SAMPLER_DESC sampler_desc{};
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
        _device->CreateSamplerState(&sampler_desc, _sampler_linear_wrap.clear_and_assign());

        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        _device->CreateSamplerState(&sampler_desc, _sampler_point_clamp.clear_and_assign());

        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        _device->CreateSamplerState(&sampler_desc, _sampler_linear_clamp.clear_and_assign());

        // Blend state: premultiplied alpha
        CD3D11_BLEND_DESC blend_desc{CD3D11_DEFAULT{}};
        blend_desc.RenderTarget[0].BlendEnable = TRUE;
        blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;  // Premultiplied
        blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        _device->CreateBlendState(&blend_desc, _blend_state.clear_and_assign());

        // Depth stencil: read only, no write (for cloud pass)
        D3D11_DEPTH_STENCIL_DESC ds_desc{};
        ds_desc.DepthEnable = TRUE;
        ds_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        ds_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        _device->CreateDepthStencilState(&ds_desc, _depth_state.clear_and_assign());

        // Depth stencil: disabled (for upsample pass)
        ds_desc.DepthEnable = FALSE;
        _device->CreateDepthStencilState(&ds_desc, _depth_state_disabled.clear_and_assign());

        // Rasterizer: no culling for billboards
        D3D11_RASTERIZER_DESC rs_desc{};
        rs_desc.FillMode = D3D11_FILL_SOLID;
        rs_desc.CullMode = D3D11_CULL_NONE;
        rs_desc.DepthClipEnable = TRUE;
        _device->CreateRasterizerState(&rs_desc, _raster_state.clear_and_assign());
    }

    void set_params(const Cloud_params_bf3& params) noexcept
    {
        bool needs_rebuild = params.volumes.size() != _params.volumes.size();
        
        if (!needs_rebuild) {
            for (size_t i = 0; i < params.volumes.size(); ++i) {
                const auto& a = params.volumes[i];
                const auto& b = _params.volumes[i];
                if (a.cluster_count != b.cluster_count || 
                    a.particles_per_cluster != b.particles_per_cluster ||
                    a.position != b.position ||
                    a.scale != b.scale) {
                    needs_rebuild = true;
                    break;
                }
            }
        }

        // Also rebuild if particle size or cluster radius changed significantly
        if (std::abs(params.particle_size_min - _params.particle_size_min) > 1.0f ||
            std::abs(params.particle_size_max - _params.particle_size_max) > 1.0f ||
            std::abs(params.cluster_radius - _params.cluster_radius) > 1.0f) {
            needs_rebuild = true;
        }

        _params = params;
        
        if (needs_rebuild) {
            rebuild_particles();
        }
        
        _particles_dirty = true;
    }

    const Cloud_params_bf3& get_params() const noexcept { return _params; }

    void render(ID3D11DeviceContext1& dc,
                Rendertarget_allocator& rt_allocator,
                ID3D11RenderTargetView& output_rtv,
                ID3D11DepthStencilView& depth_dsv,
                ID3D11ShaderResourceView& depth_srv,
                const core::Shader_resource_database& textures,
                const Clouds_bf3_input& input,
                std::array<ID3D11UnorderedAccessView*, 3> oit_uavs,
                Profiler& profiler) noexcept
    {
        (void)depth_dsv;     // Not used - we sample depth instead

        if (!_params.enabled) {
            return;
        }
        
        if (_particles.empty()) {
            rebuild_particles();
            if (_particles.empty()) {
                return;
            }
        }

        // Load noise texture from database if not yet loaded
        if (!_noise_texture) {
            _noise_texture = textures.at_if("_SP_BUILTIN_cloud_noise"s);
            if (!_noise_texture) {
                _noise_texture = textures.at_if("_SP_BUILTIN_white"s);
            }
            if (!_noise_texture) {
                return;
            }
        }

        // Cache sun values - only update color if direction matches cached direction
        float input_dir_len = glm::length(input.sun_direction);
        if (input_dir_len > 0.1f) {
            glm::vec3 input_dir_normalized = input.sun_direction / input_dir_len;
            float dir_similarity = glm::dot(input_dir_normalized, _cached_sun_dir);

            // First valid frame, or direction matches (dot product ~1.0)
            if (glm::length(_cached_sun_color) < 0.01f || dir_similarity > 0.99f) {
                _cached_sun_dir = input_dir_normalized;
                if (glm::length(input.sun_color) > 0.1f) {
                    _cached_sun_color = input.sun_color;
                }
            }
        }

        Profile profile{profiler, dc, "Clouds BF3"sv};

        upload_particles(dc);

        // =====================================================================
        // PASS 1: Render clouds to low-resolution buffer (1/4 resolution)
        // =====================================================================
        
        const UINT lowres_width = std::max(1u, input.width / 4);
        const UINT lowres_height = std::max(1u, input.height / 4);
        
        // Allocate low-res render target
        Rendertarget_desc lowres_desc{};
        lowres_desc.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        lowres_desc.width = lowres_width;
        lowres_desc.height = lowres_height;
        lowres_desc.bind_flags = rendertarget_bind_srv_rtv;
        
        auto lowres_rt = rt_allocator.allocate(lowres_desc);
        
        // Clear low-res target to transparent
        const float clear_color[4] = {0, 0, 0, 0};
        dc.ClearRenderTargetView(lowres_rt.rtv(), clear_color);

        // Build constant buffer
        GPU_CloudConstants constants{};
        
        constants.view_matrix = input.view_matrix;
        constants.proj_matrix = input.proj_matrix;
        constants.view_proj_matrix = input.proj_matrix * input.view_matrix;
        
        constants.camera_position = input.camera_position;
        constants.cloud_time = input.time;
        
        glm::mat4 inv_view = glm::inverse(input.view_matrix);
        constants.camera_right = glm::vec3(inv_view[0]);
        constants.camera_up = glm::vec3(inv_view[1]);
        constants.camera_forward = -glm::vec3(inv_view[2]);
        
        constants.global_alpha = _params.global_alpha;
        constants.fade_near = _params.fade_near;
        constants.fade_far = _params.fade_far;
        
        // Use FULL-RES screen size for depth UV calculation (depth texture is full res)
        constants.screen_size = glm::vec2(input.width, input.height);
        constants.depth_params = glm::vec2(input.proj_matrix[2][2], input.proj_matrix[3][2]);
        
        constants.sun_direction = _cached_sun_dir;
        constants.sun_intensity = _params.sun_intensity;
        constants.sun_color = _cached_sun_color;
        constants.ambient_intensity = _params.ambient_intensity;
        
        constants.cloud_color_bright = _params.color_bright;
        constants.cloud_color_dark = _params.color_dark;
        constants.cloud_color_ambient = _params.color_ambient;
        
        constants.octave_weights = _params.octave_weights;
        constants.noise_scale = _params.noise_scale;
        constants.noise_erosion = _params.noise_erosion;
        constants.noise_edge_sharpness = _params.noise_edge_sharpness;
        constants.noise_animation_speed = _params.noise_animation_speed;
        
        constants.scatter_forward = _params.scatter_forward;
        constants.scatter_forward_exp = _params.scatter_forward_exp;
        constants.scatter_back = _params.scatter_back;
        constants.absorption = _params.absorption;
        
        constants.powder_strength = _params.powder_strength;
        constants.ambient_boost = _params.ambient_boost;
        constants.self_shadow = _params.self_shadow;
        constants.depth_softness = _params.depth_softness;

        core::update_dynamic_buffer(dc, *_constant_buffer, constants);

        // Set up pipeline state for Pass 1
        dc.VSSetShader(_vertex_shader.get(), nullptr, 0);
        dc.PSSetShader(_pixel_shader.get(), nullptr, 0);

        ID3D11ShaderResourceView* srvs[] = {
            &depth_srv,
            _particle_texture.get(),
            _particle_buffer_srv.get(),
            _noise_texture.get()
        };
        dc.VSSetShaderResources(0, 4, srvs);
        dc.PSSetShaderResources(0, 4, srvs);

        ID3D11SamplerState* samplers[] = {
            _sampler_linear_wrap.get(),
            _sampler_point_clamp.get(),
            _sampler_linear_clamp.get()
        };
        dc.VSSetSamplers(0, 3, samplers);
        dc.PSSetSamplers(0, 3, samplers);

        ID3D11Buffer* cbs[] = { _constant_buffer.get() };
        dc.VSSetConstantBuffers(1, 1, cbs);
        dc.PSSetConstantBuffers(1, 1, cbs);

        // Render to low-res buffer
        ID3D11RenderTargetView* rtvs_lowres[] = { lowres_rt.rtv() };
        dc.OMSetRenderTargets(1, rtvs_lowres, nullptr);
        dc.OMSetBlendState(_blend_state.get(), nullptr, 0xffffffff);
        dc.OMSetDepthStencilState(_depth_state_disabled.get(), 0);
        dc.RSSetState(_raster_state.get());

        D3D11_VIEWPORT lowres_viewport{};
        lowres_viewport.Width = static_cast<float>(lowres_width);
        lowres_viewport.Height = static_cast<float>(lowres_height);
        lowres_viewport.MaxDepth = 1.0f;
        dc.RSSetViewports(1, &lowres_viewport);

        dc.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc.IASetInputLayout(nullptr);

        // Draw all particles at low-res
        dc.DrawInstanced(6, static_cast<UINT>(_particles.size()), 0, 0);

        // =====================================================================
        // PASS 2: Simple upsample to full resolution (bilinear for now)
        // =====================================================================

        // IMPORTANT: Unbind low-res RTV before binding it as SRV
        ID3D11RenderTargetView* null_rtv[] = { nullptr };
        dc.OMSetRenderTargets(1, null_rtv, nullptr);

        // Set up upsample pass
        dc.VSSetShader(_upsample_vertex_shader.get(), nullptr, 0);
        
        // Check if OIT is active
        const bool use_oit = oit_uavs[0] != nullptr && oit_uavs[1] != nullptr && oit_uavs[2] != nullptr;
        
        if (use_oit) {
            dc.PSSetShader(_upsample_oit_pixel_shader.get(), nullptr, 0);
        } else {
            dc.PSSetShader(_upsample_pixel_shader.get(), nullptr, 0);
        }

        // Build upsample constant buffer
        GPU_UpsampleConstants upsample_constants{};
        upsample_constants.lowres_size = glm::vec2(lowres_width, lowres_height);
        upsample_constants.fullres_size = glm::vec2(input.width, input.height);
        upsample_constants.texel_size = glm::vec2(1.0f / lowres_width, 1.0f / lowres_height);
        upsample_constants.depth_threshold = 0.01f;
        upsample_constants.upsample_sharpness = 1.0f;
        upsample_constants.upsample_depth_params = glm::vec2(input.proj_matrix[2][2], input.proj_matrix[3][2]);
        upsample_constants.use_firstperson_depth = (input.firstperson_depth_srv != nullptr) ? 1.0f : 0.0f;
        
        core::update_dynamic_buffer(dc, *_upsample_constant_buffer, upsample_constants);

        // Bind low-res cloud texture, scene depth, and first-person depth for upsample
        ID3D11ShaderResourceView* upsample_srvs[] = {
            lowres_rt.srv(),              // Low-res clouds at t0
            &depth_srv,                   // Scene depth at t1
            input.firstperson_depth_srv   // First-person depth at t2 (may be nullptr)
        };
        dc.PSSetShaderResources(0, 3, upsample_srvs);
        dc.PSSetSamplers(0, 3, samplers);

        // Constant buffer for upsample at b2
        ID3D11Buffer* upsample_cbs[] = { _upsample_constant_buffer.get() };
        dc.PSSetConstantBuffers(2, 1, upsample_cbs);

        D3D11_VIEWPORT fullres_viewport{};
        fullres_viewport.Width = static_cast<float>(input.width);
        fullres_viewport.Height = static_cast<float>(input.height);
        fullres_viewport.MaxDepth = 1.0f;
        dc.RSSetViewports(1, &fullres_viewport);

        if (use_oit) {
            // Bind OIT UAVs for transparency sorting
            ID3D11RenderTargetView* null_rtvs[] = { nullptr };
            dc.OMSetRenderTargetsAndUnorderedAccessViews(
                1, null_rtvs, nullptr,
                1, 3, oit_uavs.data(), nullptr);
        } else {
            // Render directly to output
            ID3D11RenderTargetView* rtvs_output[] = { &output_rtv };
            dc.OMSetRenderTargets(1, rtvs_output, nullptr);
        }

        // Draw fullscreen triangle
        dc.Draw(3, 0);

        // Cleanup
        ID3D11ShaderResourceView* null_srvs[5] = {};
        dc.VSSetShaderResources(0, 5, null_srvs);
        dc.PSSetShaderResources(0, 5, null_srvs);
        
        if (use_oit) {
            ID3D11UnorderedAccessView* null_uavs[3] = {};
            dc.OMSetRenderTargetsAndUnorderedAccessViews(
                0, nullptr, nullptr,
                1, 3, null_uavs, nullptr);
        }
    }

private:
    void rebuild_particles()
    {
        _particles.clear();
        _particles.reserve(max_particles);

        std::mt19937 rng(12345);
        std::uniform_real_distribution<float> dist_01(0.0f, 1.0f);
        std::uniform_real_distribution<float> dist_angle(0.0f, 6.28318f);
        std::uniform_real_distribution<float> dist_signed(-1.0f, 1.0f);

        for (const auto& vol : _params.volumes) {
            for (int c = 0; c < vol.cluster_count && _particles.size() < max_particles; ++c) {
                
                glm::vec3 cluster_center;
                {
                    float u = dist_01(rng);
                    float v = dist_01(rng);
                    float theta = 2.0f * 3.14159f * u;
                    float phi = std::acos(2.0f * v - 1.0f);
                    float r = std::cbrt(dist_01(rng));

                    glm::vec3 unit_sphere;
                    unit_sphere.x = r * std::sin(phi) * std::cos(theta);
                    unit_sphere.y = r * std::sin(phi) * std::sin(theta);
                    unit_sphere.z = r * std::cos(phi);

                    cluster_center = vol.position + unit_sphere * vol.scale;
                }

                for (int p = 0; p < vol.particles_per_cluster && _particles.size() < max_particles; ++p) {
                    GPU_Particle particle;

                    glm::vec3 offset;
                    offset.x = dist_signed(rng) * _params.cluster_radius;
                    offset.y = dist_signed(rng) * _params.cluster_radius * 0.5f;
                    offset.z = dist_signed(rng) * _params.cluster_radius;

                    particle.position = cluster_center + offset;

                    particle.size = _params.particle_size_min + 
                        dist_01(rng) * (_params.particle_size_max - _params.particle_size_min);

                    particle.color = glm::vec3(1.0f);
                    particle.alpha = 0.5f + dist_01(rng) * 0.5f;
                    particle.rotation = dist_angle(rng);
                    particle.noise_offset = dist_01(rng);
                    particle.density = vol.density * (0.7f + dist_01(rng) * 0.6f);
                    particle._pad = 0.0f;

                    _particles.push_back(particle);
                }
            }
        }

        _particles_dirty = true;
    }

    void upload_particles(ID3D11DeviceContext1& dc)
    {
        if (!_particles_dirty || _particles.empty()) return;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc.Map(_particle_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            const size_t copy_size = std::min(_particles.size(), static_cast<size_t>(max_particles)) 
                                     * sizeof(GPU_Particle);
            std::memcpy(mapped.pData, _particles.data(), copy_size);
            dc.Unmap(_particle_buffer.get(), 0);
        }

        _particles_dirty = false;
    }

    Com_ptr<ID3D11Device5> _device;

    // Shaders
    Com_ptr<ID3D11VertexShader> _vertex_shader;
    Com_ptr<ID3D11PixelShader> _pixel_shader;
    Com_ptr<ID3D11VertexShader> _fog_vertex_shader;
    Com_ptr<ID3D11PixelShader> _fog_pixel_shader;
    Com_ptr<ID3D11VertexShader> _upsample_vertex_shader;
    Com_ptr<ID3D11PixelShader> _upsample_pixel_shader;
    Com_ptr<ID3D11PixelShader> _upsample_oit_pixel_shader;

    // Textures
    Com_ptr<ID3D11ShaderResourceView> _particle_texture;
    Com_ptr<ID3D11ShaderResourceView> _noise_texture;

    // Buffers
    Com_ptr<ID3D11Buffer> _particle_buffer;
    Com_ptr<ID3D11ShaderResourceView> _particle_buffer_srv;
    Com_ptr<ID3D11Buffer> _constant_buffer;
    Com_ptr<ID3D11Buffer> _upsample_constant_buffer;

    // Samplers
    Com_ptr<ID3D11SamplerState> _sampler_linear_wrap;
    Com_ptr<ID3D11SamplerState> _sampler_point_clamp;
    Com_ptr<ID3D11SamplerState> _sampler_linear_clamp;

    // States
    Com_ptr<ID3D11BlendState> _blend_state;
    Com_ptr<ID3D11DepthStencilState> _depth_state;
    Com_ptr<ID3D11DepthStencilState> _depth_state_disabled;
    Com_ptr<ID3D11RasterizerState> _raster_state;

    // Data
    Cloud_params_bf3 _params;
    std::vector<GPU_Particle> _particles;
    bool _particles_dirty = true;
    glm::vec3 _cached_sun_dir = glm::normalize(glm::vec3(0.5f, 0.8f, 0.3f));
    glm::vec3 _cached_sun_color = glm::vec3(1.0f, 0.95f, 0.9f);
};

// ===========================================================================
// PUBLIC INTERFACE
// ===========================================================================

Clouds_bf3::Clouds_bf3(Com_ptr<ID3D11Device5> device, shader::Database& shaders)
    : _impl{std::make_unique<Impl>(std::move(device), shaders)}
{
}

Clouds_bf3::~Clouds_bf3() = default;

void Clouds_bf3::set_params(const Cloud_params_bf3& params) noexcept
{
    _impl->set_params(params);
}

const Cloud_params_bf3& Clouds_bf3::get_params() const noexcept
{
    return _impl->get_params();
}

void Clouds_bf3::render(ID3D11DeviceContext1& dc,
                        Rendertarget_allocator& rt_allocator,
                        ID3D11RenderTargetView& output_rtv,
                        ID3D11DepthStencilView& depth_dsv,
                        ID3D11ShaderResourceView& depth_srv,
                        const core::Shader_resource_database& textures,
                        const Clouds_bf3_input& input,
                        std::array<ID3D11UnorderedAccessView*, 3> oit_uavs,
                        Profiler& profiler) noexcept
{
    _impl->render(dc, rt_allocator, output_rtv, depth_dsv, depth_srv, textures, input, oit_uavs, profiler);
}

} // namespace sp::effects

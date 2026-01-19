
#include "debug_visualizer.hpp"
#include "../imgui/imgui.h"
#include "../user_config.hpp"

#include <Windows.h>

namespace sp::core {

using namespace std::literals;

//------------------------------------------------------------------------------
// Constructor
//------------------------------------------------------------------------------

Debug_visualizer::Debug_visualizer(Com_ptr<ID3D11Device5> device,
                                   shader::Database& shaders) noexcept
   : _device{std::move(device)}
{
    init_shaders(shaders);
    init_constant_buffers();
    init_blend_state();
    init_depth_stencil_states();
    init_sampler_state();

    // Load config from user settings
    _config.toggle_hotkey = user_config.developer.debug_visualizer_toggle_key;
    _config.cycle_hotkey = user_config.developer.debug_visualizer_cycle_key;
    _config.max_depth_distance = user_config.developer.debug_visualizer_max_depth;
    _config.stencil_overlay_alpha = user_config.developer.debug_visualizer_stencil_alpha;
    _config.stencil_max_ref = user_config.developer.debug_visualizer_stencil_max_ref;
}

//------------------------------------------------------------------------------
// Initialization
//------------------------------------------------------------------------------

void Debug_visualizer::init_shaders(shader::Database& shaders) noexcept
{
    // Use existing postprocess vertex shader for fullscreen triangle
    // (same approach as pixel_inspector - no custom VS needed)
    _vs = std::get<0>(shaders.vertex("postprocess"sv).entrypoint("main_vs"sv));

    // Load pixel shaders from debug visualizer group
    _depth_ps = shaders.pixel("debug visualizer"sv).entrypoint("depth_ps"sv);
    _stencil_ps = shaders.pixel("debug visualizer"sv).entrypoint("stencil_ps"sv);
}

void Debug_visualizer::init_constant_buffers() noexcept
{
    // Depth visualization constant buffer
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(Debug_depth_cb);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        _device->CreateBuffer(&desc, nullptr, _depth_cb.clear_and_assign());
    }

    // Stencil visualization constant buffer
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(Debug_stencil_cb);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        _device->CreateBuffer(&desc, nullptr, _stencil_cb.clear_and_assign());
    }
}

void Debug_visualizer::init_blend_state() noexcept
{
    // Alpha blending for stencil overlay
    {
        D3D11_BLEND_DESC desc = {};
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        _device->CreateBlendState(&desc, _overlay_blend_state.clear_and_assign());
    }

    // Opaque (no blending) for depth visualization base
    {
        D3D11_BLEND_DESC desc = {};
        desc.RenderTarget[0].BlendEnable = FALSE;
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        _device->CreateBlendState(&desc, _opaque_blend_state.clear_and_assign());
    }
}

void Debug_visualizer::init_depth_stencil_states() noexcept
{
    // State with depth/stencil disabled (for depth visualization)
    {
        D3D11_DEPTH_STENCIL_DESC desc = {};
        desc.DepthEnable = FALSE;
        desc.StencilEnable = FALSE;

        _device->CreateDepthStencilState(&desc, _ds_disabled.clear_and_assign());
    }

    // Stencil test: NOT EQUAL to zero (passes where stencil != 0)
    {
        D3D11_DEPTH_STENCIL_DESC desc = {};
        desc.DepthEnable = FALSE;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.StencilEnable = TRUE;
        desc.StencilReadMask = 0xFF;
        desc.StencilWriteMask = 0x00;  // Never write to stencil

        desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
        desc.BackFace = desc.FrontFace;

        _device->CreateDepthStencilState(&desc, _ds_stencil_notequal_zero.clear_and_assign());
    }

    // Debug: stencil test ALWAYS passes (to verify stencil testing pipeline works)
    {
        D3D11_DEPTH_STENCIL_DESC desc = {};
        desc.DepthEnable = FALSE;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.StencilEnable = TRUE;
        desc.StencilReadMask = 0xFF;
        desc.StencilWriteMask = 0x00;

        desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;  // Always pass
        desc.BackFace = desc.FrontFace;

        _device->CreateDepthStencilState(&desc, _ds_stencil_always.clear_and_assign());
    }

    // Bitmask states: test if specific bit is set
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
        D3D11_DEPTH_STENCIL_DESC desc = {};
        desc.DepthEnable = FALSE;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.StencilEnable = TRUE;
        desc.StencilReadMask = static_cast<UINT8>(1 << bit);  // Only test this bit
        desc.StencilWriteMask = 0x00;

        desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;  // Passes if masked value != 0
        desc.BackFace = desc.FrontFace;

        _device->CreateDepthStencilState(&desc, _ds_stencil_bitmask[bit].clear_and_assign());
    }

    // Equal states are created lazily in get_stencil_equal_state()
}

void Debug_visualizer::init_sampler_state() noexcept
{
    D3D11_SAMPLER_DESC desc = {};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;  // Point sampling for depth
    desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    _device->CreateSamplerState(&desc, _point_sampler.clear_and_assign());
}

//------------------------------------------------------------------------------
// Lazy Initialization
//------------------------------------------------------------------------------

ID3D11DepthStencilState* Debug_visualizer::get_stencil_equal_state(std::uint8_t ref) noexcept
{
    // Lazy-create stencil EQUAL state for this reference value
    if (!_ds_stencil_equal[ref]) {
        D3D11_DEPTH_STENCIL_DESC desc = {};
        desc.DepthEnable = FALSE;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.StencilEnable = TRUE;
        desc.StencilReadMask = 0xFF;
        desc.StencilWriteMask = 0x00;

        desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;  // Passes if stencil == ref
        desc.BackFace = desc.FrontFace;

        _device->CreateDepthStencilState(&desc, _ds_stencil_equal[ref].clear_and_assign());
    }

    return _ds_stencil_equal[ref].get();
}

//------------------------------------------------------------------------------
// Input Handling
//------------------------------------------------------------------------------

void Debug_visualizer::update_input() noexcept
{
    // Toggle visualizer on/off
    if (_config.toggle_hotkey != 0 && key_just_pressed(_config.toggle_hotkey)) {
        if (_config.mode == Debug_visualizer_mode::none) {
            // Restore last active mode, or default to depth_linear
            _config.mode = _last_active_mode;
            if (_config.mode == Debug_visualizer_mode::none) {
                _config.mode = Debug_visualizer_mode::depth_linear;
            }
        }
        else {
            // Save current mode and disable
            _last_active_mode = _config.mode;
            _config.mode = Debug_visualizer_mode::none;
        }
    }

    // Cycle through modes
    if (_config.cycle_hotkey != 0 && key_just_pressed(_config.cycle_hotkey)) {
        cycle_mode();
    }
}

bool Debug_visualizer::key_just_pressed(std::uint32_t vk_code) noexcept
{
    if (vk_code >= 256) return false;

    bool currently_down = (GetAsyncKeyState(static_cast<int>(vk_code)) & 0x8000) != 0;
    bool was_down = _key_states[vk_code];
    _key_states[vk_code] = currently_down;

    return currently_down && !was_down;
}

void Debug_visualizer::cycle_mode() noexcept
{
    auto next = static_cast<std::uint8_t>(_config.mode) + 1;
    if (next >= static_cast<std::uint8_t>(Debug_visualizer_mode::_count)) {
        next = 0;
    }
    _config.mode = static_cast<Debug_visualizer_mode>(next);
}

//------------------------------------------------------------------------------
// Constant Buffer Updates
//------------------------------------------------------------------------------

void Debug_visualizer::update_depth_constants(const glm::mat4& nearscene_proj,
                                               const glm::mat4& farscene_proj) noexcept
{
    // Extract depth linearization parameters from projection matrix.
    // For standard perspective projection (column-major, GLM convention):
    //   proj[2][2] = far / (far - near)           [or negated depending on handedness]
    //   proj[3][2] = -(far * near) / (far - near)
    //
    // Linearization formula: linear_z = mul / (add - raw_depth)
    // where mul = (far * near) / (far - near), add = far / (far - near)

    // Nearscene linearization params
    float linearize_mul = -nearscene_proj[3][2];
    float linearize_add = nearscene_proj[2][2];
    if (linearize_mul * linearize_add < 0) {
        linearize_add = -linearize_add;
    }
    _depth_cb_data.depth_linearize_params = glm::vec2(linearize_mul, linearize_add);

    // Farscene linearization params
    float linearize_mul_far = -farscene_proj[3][2];
    float linearize_add_far = farscene_proj[2][2];
    if (linearize_mul_far * linearize_add_far < 0) {
        linearize_add_far = -linearize_add_far;
    }
    _depth_cb_data.depth_linearize_params_far = glm::vec2(linearize_mul_far, linearize_add_far);

    _depth_cb_data.max_depth_distance = _config.max_depth_distance;
    _depth_cb_data.log_scale_factor = _config.log_scale_factor;
    _depth_cb_data.brightness = _config.depth_brightness;
    _depth_cb_data.sky_threshold = _config.sky_threshold;
    _depth_cb_data.near_color = _config.depth_near_color;
    _depth_cb_data.far_color = _config.depth_far_color;
    _depth_cb_data.sky_color = _config.depth_sky_color;
    // 0 = nearscene only, 1 = combine both, 2 = farscene only
    _depth_cb_data.use_dual_buffers = _config.view_farscene_only ? 2 :
                                      (_config.use_dual_depth_buffers ? 1 : 0);

    // Map mode enum to shader constant
    switch (_config.mode) {
        case Debug_visualizer_mode::depth_linear:
        case Debug_visualizer_mode::combined:
            _depth_cb_data.visualization_mode = 0;
            break;
        case Debug_visualizer_mode::depth_log:
            _depth_cb_data.visualization_mode = 1;
            break;
        case Debug_visualizer_mode::depth_raw:
            _depth_cb_data.visualization_mode = 2;
            break;
        default:
            _depth_cb_data.visualization_mode = 0;
            break;
    }
}

void Debug_visualizer::update_stencil_constants(const glm::vec4& color) noexcept
{
    _stencil_cb_data.overlay_color = color;
}

//------------------------------------------------------------------------------
// Rendering
//------------------------------------------------------------------------------

void Debug_visualizer::render(ID3D11DeviceContext4& dc,
                               ID3D11RenderTargetView* output_rtv,
                               ID3D11ShaderResourceView* nearscene_depth_srv,
                               ID3D11ShaderResourceView* farscene_depth_srv,
                               ID3D11DepthStencilView* depth_dsv_readonly,
                               UINT dsv_sample_count,
                               const glm::mat4& nearscene_proj,
                               const glm::mat4& farscene_proj,
                               std::uint32_t render_width,
                               std::uint32_t render_height) noexcept
{
    // Track for ImGui warnings
    _last_dsv_sample_count = dsv_sample_count;

    if (_config.mode == Debug_visualizer_mode::none) return;

    switch (_config.mode) {
        case Debug_visualizer_mode::depth_linear:
        case Debug_visualizer_mode::depth_log:
        case Debug_visualizer_mode::depth_raw:
            render_depth(dc, output_rtv, nearscene_depth_srv, farscene_depth_srv,
                         nearscene_proj, farscene_proj, render_width, render_height);
            break;

        case Debug_visualizer_mode::stencil_nonzero:
        case Debug_visualizer_mode::stencil_values:
        case Debug_visualizer_mode::stencil_bits:
            render_stencil(dc, output_rtv, depth_dsv_readonly, dsv_sample_count,
                           render_width, render_height);
            break;

        case Debug_visualizer_mode::combined:
            // Render depth first as base layer
            render_depth(dc, output_rtv, nearscene_depth_srv, farscene_depth_srv,
                         nearscene_proj, farscene_proj, render_width, render_height);
            // Then overlay stencil
            render_stencil(dc, output_rtv, depth_dsv_readonly, dsv_sample_count,
                           render_width, render_height);
            break;

        default:
            break;
    }

    // Cleanup: unbind resources to avoid hazards
    ID3D11ShaderResourceView* null_srvs[2] = {nullptr, nullptr};
    dc.PSSetShaderResources(0, 2, null_srvs);
    dc.OMSetRenderTargets(0, nullptr, nullptr);
}

void Debug_visualizer::render_depth(ID3D11DeviceContext4& dc,
                                     ID3D11RenderTargetView* output_rtv,
                                     ID3D11ShaderResourceView* nearscene_depth_srv,
                                     ID3D11ShaderResourceView* farscene_depth_srv,
                                     const glm::mat4& nearscene_proj,
                                     const glm::mat4& farscene_proj,
                                     std::uint32_t width, std::uint32_t height) noexcept
{
    if (!nearscene_depth_srv || !output_rtv) return;

    // Update constant buffer with both projection matrices
    update_depth_constants(nearscene_proj, farscene_proj);
    dc.UpdateSubresource(_depth_cb.get(), 0, nullptr, &_depth_cb_data, 0, 0);

    // Setup viewport
    const D3D11_VIEWPORT viewport{
        0.0f, 0.0f,
        static_cast<float>(width),
        static_cast<float>(height),
        0.0f, 1.0f
    };

    // Setup pipeline state
    dc.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc.IASetInputLayout(nullptr);  // No vertex input needed
    dc.VSSetShader(_vs.get(), nullptr, 0);
    dc.RSSetViewports(1, &viewport);
    dc.RSSetState(nullptr);  // Default rasterizer

    dc.PSSetShader(_depth_ps.get(), nullptr, 0);

    // Bind both depth textures if dual buffer mode is enabled
    ID3D11ShaderResourceView* srvs[2] = {nearscene_depth_srv, farscene_depth_srv};
    dc.PSSetShaderResources(0, 2, srvs);

    auto* sampler = _point_sampler.get();
    dc.PSSetSamplers(0, 1, &sampler);
    auto* cb = _depth_cb.get();
    dc.PSSetConstantBuffers(0, 1, &cb);

    // Output to render target, no depth testing
    const float blend_factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    dc.OMSetBlendState(_opaque_blend_state.get(), blend_factor, 0xFFFFFFFF);
    dc.OMSetDepthStencilState(_ds_disabled.get(), 0);
    dc.OMSetRenderTargets(1, &output_rtv, nullptr);

    // Draw fullscreen triangle
    dc.Draw(3, 0);
}

void Debug_visualizer::render_stencil(ID3D11DeviceContext4& dc,
                                       ID3D11RenderTargetView* output_rtv,
                                       ID3D11DepthStencilView* depth_dsv_readonly,
                                       UINT dsv_sample_count,
                                       std::uint32_t width, std::uint32_t height) noexcept
{
    if (!depth_dsv_readonly || !output_rtv) return;

    // MSAA constraint: DSV and RTV must have matching sample counts to be bound together.
    // When DSV is MSAA but swapchain is not, we cannot do proper stencil testing.
    // For standalone stencil modes, we skip rendering and rely on combined mode.
    // Combined mode works because render_depth establishes the pipeline first.
    const bool msaa_mismatch = dsv_sample_count > 1;

    // For standalone stencil modes with MSAA, we can't render properly
    // (user should use combined mode instead)
    if (msaa_mismatch && _config.mode != Debug_visualizer_mode::combined) {
        // Skip - MSAA stencil only works in combined mode
        return;
    }

    // Setup viewport
    const D3D11_VIEWPORT viewport{
        0.0f, 0.0f,
        static_cast<float>(width),
        static_cast<float>(height),
        0.0f, 1.0f
    };

    // Setup common pipeline state
    dc.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc.IASetInputLayout(nullptr);
    dc.VSSetShader(_vs.get(), nullptr, 0);
    dc.RSSetViewports(1, &viewport);
    dc.RSSetState(nullptr);

    dc.PSSetShader(_stencil_ps.get(), nullptr, 0);
    auto* cb = _stencil_cb.get();
    dc.PSSetConstantBuffers(0, 1, &cb);

    // Alpha blending for overlay
    const float blend_factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    dc.OMSetBlendState(_overlay_blend_state.get(), blend_factor, 0xFFFFFFFF);

    // Bind RTV + read-only DSV for stencil testing
    // The DSV allows stencil tests but prevents writes (we only read stencil)
    dc.OMSetRenderTargets(1, &output_rtv, depth_dsv_readonly);

    // Determine which stencil mode to use
    Combined_stencil_mode stencil_mode;
    if (_config.mode == Debug_visualizer_mode::combined) {
        stencil_mode = _config.combined_stencil_mode;
    }
    else if (_config.mode == Debug_visualizer_mode::stencil_nonzero) {
        stencil_mode = Combined_stencil_mode::nonzero;
    }
    else if (_config.mode == Debug_visualizer_mode::stencil_values) {
        stencil_mode = Combined_stencil_mode::values;
    }
    else {
        stencil_mode = Combined_stencil_mode::bitmask;
    }

    // Render based on stencil mode
    switch (stencil_mode) {
        case Combined_stencil_mode::nonzero:
            render_stencil_nonzero_pass(dc);
            break;
        case Combined_stencil_mode::values:
            render_stencil_value_passes(dc);
            break;
        case Combined_stencil_mode::bitmask:
            render_stencil_bitmask_passes(dc);
            break;
    }
}

void Debug_visualizer::render_stencil_nonzero_pass(ID3D11DeviceContext4& dc) noexcept
{
    // Use always-pass state for debugging, otherwise use not-equal-zero
    if (_config.stencil_debug_always_pass) {
        dc.OMSetDepthStencilState(_ds_stencil_always.get(), 0);
    }
    else {
        dc.OMSetDepthStencilState(_ds_stencil_notequal_zero.get(), 0);
    }

    // Semi-transparent red overlay (cyan if debug mode to distinguish)
    glm::vec4 color = _config.stencil_debug_always_pass
        ? glm::vec4(0.0f, 1.0f, 1.0f, 0.5f * _config.stencil_overlay_alpha)  // Cyan for debug
        : glm::vec4(1.0f, 0.3f, 0.3f, 0.6f * _config.stencil_overlay_alpha); // Red for normal
    update_stencil_constants(color);
    dc.UpdateSubresource(_stencil_cb.get(), 0, nullptr, &_stencil_cb_data, 0, 0);

    dc.Draw(3, 0);
}

void Debug_visualizer::render_stencil_value_passes(ID3D11DeviceContext4& dc) noexcept
{
    // One pass per stencil value (1 to max_ref)
    const std::uint8_t max_ref = _config.stencil_max_ref;

    for (std::uint8_t ref = 1; ref <= max_ref; ++ref) {
        // Get depth-stencil state for EQUAL test
        auto* ds_state = get_stencil_equal_state(ref);
        dc.OMSetDepthStencilState(ds_state, ref);

        // Color from palette, with user alpha adjustment
        glm::vec4 color;
        if (ref < stencil_value_colors.size()) {
            color = stencil_value_colors[ref];
        }
        else {
            // For values beyond palette, generate a color
            float hue = static_cast<float>(ref % 8) / 8.0f;
            color = glm::vec4(
                0.5f + 0.5f * std::sin(hue * 6.28318f),
                0.5f + 0.5f * std::sin((hue + 0.333f) * 6.28318f),
                0.5f + 0.5f * std::sin((hue + 0.666f) * 6.28318f),
                0.7f
            );
        }
        color.a *= _config.stencil_overlay_alpha;

        update_stencil_constants(color);
        dc.UpdateSubresource(_stencil_cb.get(), 0, nullptr, &_stencil_cb_data, 0, 0);

        dc.Draw(3, 0);
    }
}

void Debug_visualizer::render_stencil_bitmask_passes(ID3D11DeviceContext4& dc) noexcept
{
    // One pass per bit (0 to 7)
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
        dc.OMSetDepthStencilState(_ds_stencil_bitmask[bit].get(), 0);

        // Color from bit palette
        glm::vec4 color = stencil_bit_colors[bit];
        color.a *= _config.stencil_overlay_alpha;

        update_stencil_constants(color);
        dc.UpdateSubresource(_stencil_cb.get(), 0, nullptr, &_stencil_cb_data, 0, 0);

        dc.Draw(3, 0);
    }
}

//------------------------------------------------------------------------------
// ImGui
//------------------------------------------------------------------------------

void Debug_visualizer::show_imgui() noexcept
{
    if (!_config.show_imgui_window) return;

    if (!ImGui::Begin("Debug Visualizer", &_config.show_imgui_window,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    // Mode selection
    ImGui::Text("Visualization Mode");
    ImGui::Separator();

    int mode = static_cast<int>(_config.mode);
    for (int i = 0; i < static_cast<int>(Debug_visualizer_mode::_count); ++i) {
        if (ImGui::RadioButton(debug_visualizer_mode_names[i], mode == i)) {
            _config.mode = static_cast<Debug_visualizer_mode>(i);
        }
    }

    ImGui::Spacing();

    // Depth settings (shown for depth modes and combined)
    bool show_depth_settings =
        _config.mode == Debug_visualizer_mode::depth_linear ||
        _config.mode == Debug_visualizer_mode::depth_log ||
        _config.mode == Debug_visualizer_mode::depth_raw ||
        _config.mode == Debug_visualizer_mode::combined;

    if (show_depth_settings) {
        ImGui::Text("Depth Settings");
        ImGui::Separator();

        ImGui::SliderFloat("Max Distance", &_config.max_depth_distance,
                           10.0f, 5000.0f, "%.0f");

        if (_config.mode == Debug_visualizer_mode::depth_log) {
            ImGui::SliderFloat("Log Scale", &_config.log_scale_factor,
                               0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        }

        if (_config.mode == Debug_visualizer_mode::combined) {
            ImGui::SliderFloat("Depth Brightness", &_config.depth_brightness,
                               0.1f, 1.0f, "%.2f");
        }

        ImGui::SliderFloat("Sky Threshold", &_config.sky_threshold,
                           0.99f, 1.0f, "%.6f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Depth values >= this are shown as sky.\n"
                              "Lower = more pixels treated as sky.\n"
                              "Higher = only true skybox shown as sky.\n"
                              "Default: 0.9999");
        }

        // Dual depth buffer mode
        ImGui::Checkbox("Combine Near+Far Buffers", &_config.use_dual_depth_buffers);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Sample both nearscene and farscene depth buffers.\n"
                              "Takes the minimum (closest) depth from both.\n"
                              "Use this to see far terrain/skybox that renders\n"
                              "to a separate buffer with different projection.");
        }

        ImGui::Checkbox("View Farscene Only", &_config.view_farscene_only);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Debug: View only the farscene depth buffer.\n"
                              "Use this to see what's actually in the farscene buffer.");
        }

        // Depth colors
        ImGui::ColorEdit3("Near Color", &_config.depth_near_color.x,
                          ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::ColorEdit3("Far Color", &_config.depth_far_color.x,
                          ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::ColorEdit3("Sky Color", &_config.depth_sky_color.x,
                          ImGuiColorEditFlags_NoInputs);

        // Debug option to capture depth before clear
        ImGui::Checkbox("Capture Pre-Clear Depth", &_config.capture_depth_before_clear);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If checked, captures the depth buffer before it gets cleared.\n"
                              "This preserves world depth that would be lost when\n"
                              "clearing for first-person model rendering.");
        }

        // Show depth source status
        ImGui::Spacing();
        ImGui::Text("Depth Source:");
        ImGui::SameLine();
        if (_using_captured_depth) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Pre-Clear Capture");
        }
        else if (_captured_depth_available) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "Live (capture available)");
        }
        else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Live (no capture)");
        }

        ImGui::Spacing();
    }

    // Stencil settings (shown for stencil modes and combined)
    bool show_stencil_settings =
        _config.mode == Debug_visualizer_mode::stencil_nonzero ||
        _config.mode == Debug_visualizer_mode::stencil_values ||
        _config.mode == Debug_visualizer_mode::stencil_bits ||
        _config.mode == Debug_visualizer_mode::combined;

    if (show_stencil_settings) {
        ImGui::Text("Stencil Settings");
        ImGui::Separator();

        // MSAA warning for standalone stencil modes
        bool is_standalone_stencil =
            _config.mode == Debug_visualizer_mode::stencil_nonzero ||
            _config.mode == Debug_visualizer_mode::stencil_values ||
            _config.mode == Debug_visualizer_mode::stencil_bits;

        if (_last_dsv_sample_count > 1 && is_standalone_stencil) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
            ImGui::TextWrapped("Note: MSAA is active. Standalone stencil modes "
                               "don't work with MSAA - use Combined mode instead.");
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        if (_config.mode == Debug_visualizer_mode::combined) {
            int stencil_mode = static_cast<int>(_config.combined_stencil_mode);
            if (ImGui::Combo("Stencil Mode", &stencil_mode,
                             combined_stencil_mode_names, 3)) {
                _config.combined_stencil_mode =
                    static_cast<Combined_stencil_mode>(stencil_mode);
            }
        }

        bool show_max_ref =
            _config.mode == Debug_visualizer_mode::stencil_values ||
            (_config.mode == Debug_visualizer_mode::combined &&
             _config.combined_stencil_mode == Combined_stencil_mode::values);

        if (show_max_ref) {
            int max_ref = _config.stencil_max_ref;
            if (ImGui::SliderInt("Max Stencil Ref", &max_ref, 1, 32)) {
                _config.stencil_max_ref = static_cast<std::uint8_t>(max_ref);
            }
        }

        ImGui::SliderFloat("Overlay Alpha", &_config.stencil_overlay_alpha,
                           0.1f, 1.0f, "%.2f");

        // Debug option to verify stencil testing pipeline works
        ImGui::Checkbox("Debug: Always Pass", &_config.stencil_debug_always_pass);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If checked, stencil test always passes.\n"
                              "Shows cyan overlay everywhere if stencil pipeline works.\n"
                              "If you see cyan but not normal stencil colors,\n"
                              "the game may not be writing stencil values.");
        }

        // Debug option to force reading from farscene buffer
        ImGui::Checkbox("Debug: Force Farscene Buffer", &_config.force_farscene_buffer);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If checked, always read from farscene depth-stencil buffer.\n"
                              "Use this to check which buffer contains stencil values.\n"
                              "Near/far buffers may be swapped during rendering.");
        }

        // Debug option to capture stencil before clear
        ImGui::Checkbox("Capture Pre-Clear Stencil", &_config.capture_stencil_before_clear);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If checked, captures the stencil buffer before it gets cleared.\n"
                              "This preserves stencil values that would otherwise be lost.\n"
                              "Useful for seeing what was in the buffer before the game cleared it.");
        }

        // Show stencil source status
        ImGui::Spacing();
        ImGui::Text("Stencil Source:");
        ImGui::SameLine();
        if (_using_captured_stencil) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Pre-Clear Capture");
        }
        else if (_captured_stencil_available) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "Live (capture available)");
        }
        else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Live (no capture)");
        }



        ImGui::Spacing();
    }

    // Capture settings - show for any mode that uses captures
    if (show_depth_settings || show_stencil_settings) {
        ImGui::Text("Capture Settings");
        ImGui::Separator();

        // Show clear counts for diagnostics
        ImGui::Text("Clears/frame: Depth=%d, Stencil=%d", _depth_clear_count, _stencil_clear_count);

        // Capture at present (final frame state)
        ImGui::Checkbox("Capture at Present", &_config.capture_at_present);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Capture right before frame presents.\n"
                              "Gets the complete final frame including\n"
                              "everything rendered after all clears.");
        }

        // Choose which clear to capture on (only if not capturing at present)
        if (!_config.capture_at_present) {
            int max_clears = std::max(_depth_clear_count, _stencil_clear_count);
            if (max_clears < 1) max_clears = 4;  // Default range if no clears yet
            ImGui::SliderInt("Capture on clear #", &_config.capture_on_clear_number, 1, max_clears);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Which clear to capture before.\n"
                                  "1 = first clear (often start of frame, empty)\n"
                                  "Higher = later clears (more content rendered)");
            }
        }

        // Show which buffer was captured from (diagnostic)
        if (_using_captured_depth || _using_captured_stencil) {
            ImGui::Text("Captured from:");
            ImGui::SameLine();
            if (_capture_from_farscene) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Farscene buffer");
            }
            else {
                ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "Nearscene buffer");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Which depthstencil buffer the capture was taken from.\n"
                                  "Near/far buffers may be swapped during frame.\n"
                                  "Farscene = original nearscene after swap.");
            }
        }

        // Show swap state at present time (diagnostic)
        ImGui::Text("Buffer swap state:");
        ImGui::SameLine();
        if (_frame_swapped) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Swapped (live=farscene)");
        }
        else {
            ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "Not swapped (live=nearscene)");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Whether near/far depthstencil buffers were swapped this frame.\n"
                              "When swapped, 'live' reads from farscene buffer.\n"
                              "Swap typically happens during DoF/post-process setup.");
        }

        // Projection matrix diagnostics
        if (ImGui::CollapsingHeader("Projection Diagnostics")) {
            // Extract near/far from projection matrices
            // For perspective: proj[2][2] = f/(f-n), proj[3][2] = -fn/(f-n)
            float near_proj_22 = _nearscene_proj_info[2][2];
            float near_proj_32 = _nearscene_proj_info[3][2];
            float far_proj_22 = _farscene_proj_info[2][2];
            float far_proj_32 = _farscene_proj_info[3][2];

            ImGui::Text("Nearscene proj[2][2]: %.4f", near_proj_22);
            ImGui::Text("Nearscene proj[3][2]: %.4f", near_proj_32);
            ImGui::Text("Farscene proj[2][2]:  %.4f", far_proj_22);
            ImGui::Text("Farscene proj[3][2]:  %.4f", far_proj_32);

            // Check if farscene projection looks valid
            bool farscene_valid = (far_proj_22 != 0.0f || far_proj_32 != 0.0f);
            bool projections_same = (near_proj_22 == far_proj_22 && near_proj_32 == far_proj_32);

            if (!farscene_valid) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                    "WARNING: Farscene projection not captured!");
            }
            else if (projections_same) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f),
                    "Note: Both projections identical (fallback used)");
            }
            else {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                    "Projections differ - dual buffer should work");
            }
        }

        ImGui::Spacing();
    }

    // Color legend for depth modes
    bool show_depth_legend =
        _config.mode == Debug_visualizer_mode::depth_linear ||
        _config.mode == Debug_visualizer_mode::depth_log ||
        _config.mode == Debug_visualizer_mode::depth_raw ||
        _config.mode == Debug_visualizer_mode::combined;

    if (show_depth_legend) {
        ImGui::Text("Depth Colors");
        ImGui::Separator();

        ImGui::ColorButton("##near", ImVec4(_config.depth_near_color.r,
                                             _config.depth_near_color.g,
                                             _config.depth_near_color.b, 1.0f),
                           ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
        ImGui::SameLine();
        ImGui::Text("= Near (close)");

        ImGui::ColorButton("##far", ImVec4(_config.depth_far_color.r,
                                            _config.depth_far_color.g,
                                            _config.depth_far_color.b, 1.0f),
                           ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
        ImGui::SameLine();
        ImGui::Text("= Far (distant)");

        ImGui::ColorButton("##sky", ImVec4(_config.depth_sky_color.r,
                                            _config.depth_sky_color.g,
                                            _config.depth_sky_color.b, 1.0f),
                           ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
        ImGui::SameLine();
        ImGui::Text("= Sky/cleared");

        ImGui::Spacing();
    }

    // Color legend for non-zero stencil mode
    bool show_nonzero_legend =
        _config.mode == Debug_visualizer_mode::stencil_nonzero ||
        (_config.mode == Debug_visualizer_mode::combined &&
         _config.combined_stencil_mode == Combined_stencil_mode::nonzero);

    if (show_nonzero_legend) {
        ImGui::Text("Stencil Colors");
        ImGui::Separator();

        if (_config.stencil_debug_always_pass) {
            ImGui::ColorButton("##nonzero", ImVec4(0.0f, 1.0f, 1.0f, 1.0f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
            ImGui::SameLine();
            ImGui::Text("= Debug overlay (cyan)");
        }
        else {
            ImGui::ColorButton("##nonzero", ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
            ImGui::SameLine();
            ImGui::Text("= Stencil != 0");
        }

        ImGui::Spacing();
    }

    // Color legend for value mode
    bool show_value_legend =
        _config.mode == Debug_visualizer_mode::stencil_values ||
        (_config.mode == Debug_visualizer_mode::combined &&
         _config.combined_stencil_mode == Combined_stencil_mode::values);

    if (show_value_legend) {
        ImGui::Text("Stencil Value Colors");
        ImGui::Separator();

        for (int i = 1; i <= std::min<int>(_config.stencil_max_ref, 8); ++i) {
            ImVec4 color = ImVec4(
                stencil_value_colors[i].r,
                stencil_value_colors[i].g,
                stencil_value_colors[i].b,
                1.0f
            );
            ImGui::ColorButton(("##val" + std::to_string(i)).c_str(),
                               color, ImGuiColorEditFlags_NoTooltip,
                               ImVec2(20, 20));
            ImGui::SameLine();
            ImGui::Text("= %d", i);
            if (i % 4 != 0) ImGui::SameLine(0, 20);
        }

        ImGui::Spacing();
    }

    // Color legend for bitmask mode
    bool show_bit_legend =
        _config.mode == Debug_visualizer_mode::stencil_bits ||
        (_config.mode == Debug_visualizer_mode::combined &&
         _config.combined_stencil_mode == Combined_stencil_mode::bitmask);

    if (show_bit_legend) {
        ImGui::Text("Stencil Bit Colors");
        ImGui::Separator();

        for (int i = 0; i < 8; ++i) {
            ImVec4 color = ImVec4(
                stencil_bit_colors[i].r,
                stencil_bit_colors[i].g,
                stencil_bit_colors[i].b,
                1.0f
            );
            ImGui::ColorButton(("##bit" + std::to_string(i)).c_str(),
                               color, ImGuiColorEditFlags_NoTooltip,
                               ImVec2(20, 20));
            ImGui::SameLine();
            ImGui::Text("= Bit %d (0x%02X)", i, 1 << i);
            if (i % 2 != 1) ImGui::SameLine(0, 20);
        }

        ImGui::Spacing();
    }

    // Help text
    ImGui::Separator();
    ImGui::TextDisabled("Hotkeys configured in shader patch.yml");
    ImGui::TextDisabled("  Toggle: DebugVisualizer.ToggleKey");
    ImGui::TextDisabled("  Cycle:  DebugVisualizer.CycleKey");

    ImGui::End();
}

} // namespace sp::core

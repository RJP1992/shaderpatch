#pragma once

// debug_visualizer.hpp
// Debug visualization system for depth buffers and stencil masks.
//
// This module provides production-quality debug visualization for:
// - Depth buffer inspection (linear, logarithmic, raw modes)
// - Stencil mask inspection (non-zero, per-value, per-bit modes)
// - Combined depth+stencil overlay mode
//
// KEY CONSTRAINT: Stencil values cannot be sampled in D3D9/D3D11 shaders.
// Stencil visualization uses hardware stencil tests with fullscreen quads.

#include "com_ptr.hpp"
#include "../shader/database.hpp"

#include <array>
#include <cstdint>

#include <d3d11_4.h>
#include <glm/glm.hpp>

namespace sp::core {

//------------------------------------------------------------------------------
// Enumerations
//------------------------------------------------------------------------------

// Debug visualization modes
enum class Debug_visualizer_mode : std::uint8_t {
    none = 0,            // Disabled (default, zero overhead)

    // Depth-only modes
    depth_linear = 1,    // Linear depth mapping (uniform gradient)
    depth_log = 2,       // Logarithmic mapping (better for large distances)
    depth_raw = 3,       // Raw buffer values (debug projection issues)

    // Stencil-only modes
    stencil_nonzero = 4, // Highlight any non-zero stencil
    stencil_values = 5,  // Color-code each stencil value (1-N)
    stencil_bits = 6,    // Color-code each stencil bit (0-7)

    // Combined mode
    combined = 7,        // Depth as base, stencil overlay on top

    _count               // For iteration/cycling
};

// Stencil sub-mode for combined visualization
enum class Combined_stencil_mode : std::uint8_t {
    nonzero = 0,   // Single color for any non-zero stencil
    values = 1,    // Color per stencil value
    bitmask = 2    // Color per stencil bit
};

//------------------------------------------------------------------------------
// Mode Names (for UI/logging)
//------------------------------------------------------------------------------

constexpr const char* debug_visualizer_mode_names[] = {
    "None",
    "Depth (Linear)",
    "Depth (Logarithmic)",
    "Depth (Raw)",
    "Stencil (Non-Zero)",
    "Stencil (Values)",
    "Stencil (Bitmask)",
    "Combined"
};

constexpr const char* combined_stencil_mode_names[] = {
    "Non-Zero",
    "Values",
    "Bitmask"
};

//------------------------------------------------------------------------------
// Color Palettes
//------------------------------------------------------------------------------

// Stencil value colors (indices 1-8, index 0 unused)
// Designed for visibility and colorblind-friendliness
constexpr std::array<glm::vec4, 9> stencil_value_colors = {{
    {0.0f, 0.0f, 0.0f, 0.0f},  // 0: transparent (not rendered)
    {1.0f, 0.2f, 0.2f, 0.7f},  // 1: Red
    {0.2f, 1.0f, 0.2f, 0.7f},  // 2: Green
    {0.2f, 0.4f, 1.0f, 0.7f},  // 3: Blue
    {1.0f, 1.0f, 0.2f, 0.7f},  // 4: Yellow
    {1.0f, 0.2f, 1.0f, 0.7f},  // 5: Magenta
    {0.2f, 1.0f, 1.0f, 0.7f},  // 6: Cyan
    {1.0f, 0.6f, 0.2f, 0.7f},  // 7: Orange
    {0.6f, 0.2f, 1.0f, 0.7f},  // 8: Purple
}};

// Stencil bit colors (one per bit 0-7)
constexpr std::array<glm::vec4, 8> stencil_bit_colors = {{
    {1.0f, 0.0f, 0.0f, 0.5f},  // Bit 0: Red
    {0.0f, 1.0f, 0.0f, 0.5f},  // Bit 1: Green
    {0.0f, 0.0f, 1.0f, 0.5f},  // Bit 2: Blue
    {1.0f, 1.0f, 0.0f, 0.5f},  // Bit 3: Yellow
    {1.0f, 0.0f, 1.0f, 0.5f},  // Bit 4: Magenta
    {0.0f, 1.0f, 1.0f, 0.5f},  // Bit 5: Cyan
    {1.0f, 0.5f, 0.0f, 0.5f},  // Bit 6: Orange
    {0.5f, 0.0f, 1.0f, 0.5f},  // Bit 7: Purple
}};

//------------------------------------------------------------------------------
// Configuration
//------------------------------------------------------------------------------

struct Debug_visualizer_config {
    // Master control
    Debug_visualizer_mode mode = Debug_visualizer_mode::none;

    // Depth settings
    float max_depth_distance = 500.0f;   // World units for normalization
    float log_scale_factor = 0.01f;      // Log curve steepness (smaller = more contrast at distance)
    float depth_brightness = 0.85f;      // Brightness multiplier for combined mode
    float sky_threshold = 0.9999f;       // Depth values >= this are treated as sky

    // Stencil settings
    std::uint8_t stencil_max_ref = 8;    // Max stencil values to visualize (1-255)
    Combined_stencil_mode combined_stencil_mode = Combined_stencil_mode::values;
    float stencil_overlay_alpha = 0.6f;  // Overlay transparency multiplier

    // Controls
    std::uint32_t toggle_hotkey = 0;     // Virtual key code (0 = disabled)
    std::uint32_t cycle_hotkey = 0;      // Key to cycle modes
    bool show_imgui_window = false;      // Show settings panel

    // Diagnostic: if true, stencil test always passes (to verify pipeline works)
    bool stencil_debug_always_pass = false;

    // Diagnostic: if true, use farscene buffer instead of auto-selected buffer
    bool force_farscene_buffer = false;

    // Diagnostic: capture buffers before they get cleared
    bool capture_stencil_before_clear = true;
    bool capture_depth_before_clear = true;
    int capture_on_clear_number = 1;  // Which clear to capture on (1 = first, 2 = second, etc.)
    bool capture_at_present = false;  // Capture right before present instead of before clear

    // Depth visualization colors
    glm::vec3 depth_near_color = {1.0f, 1.0f, 1.0f};  // White for near
    glm::vec3 depth_far_color = {0.0f, 0.0f, 0.0f};   // Black for far
    glm::vec3 depth_sky_color = {0.0f, 0.0f, 0.1f};   // Dark blue for sky

    // Dual depth buffer mode: combine nearscene and farscene depth
    bool use_dual_depth_buffers = false;  // If true, sample both buffers and take min
    bool view_farscene_only = false;      // Debug: view only farscene buffer
};

//------------------------------------------------------------------------------
// Shader Constant Buffers (must match HLSL)
//------------------------------------------------------------------------------

// Constant buffer for depth visualization shader
struct alignas(16) Debug_depth_cb {
    glm::vec2 depth_linearize_params;       // x = mul, y = add (nearscene)
    float max_depth_distance;
    std::uint32_t visualization_mode;       // 0=linear, 1=log, 2=raw
    float log_scale_factor;
    float brightness;
    float sky_threshold;                    // Depth >= this is treated as sky
    std::uint32_t use_dual_buffers;         // 0=nearscene only, 1=combine both, 2=farscene only
    glm::vec3 near_color;                   // Color for near objects
    float _padding2;
    glm::vec3 far_color;                    // Color for far objects
    float _padding3;
    glm::vec3 sky_color;                    // Color for sky/cleared depth
    float _padding4;
    glm::vec2 depth_linearize_params_far;   // x = mul, y = add (farscene)
    glm::vec2 _padding5;
};

static_assert(sizeof(Debug_depth_cb) == 96, "CB size mismatch");

// Constant buffer for stencil visualization shader
struct alignas(16) Debug_stencil_cb {
    glm::vec4 overlay_color;
};

static_assert(sizeof(Debug_stencil_cb) == 16, "CB size mismatch");

//------------------------------------------------------------------------------
// Debug Visualizer Class
//------------------------------------------------------------------------------

class Debug_visualizer {
public:
    Debug_visualizer(Com_ptr<ID3D11Device5> device, shader::Database& shaders) noexcept;

    // Non-copyable, non-movable (owns D3D resources)
    Debug_visualizer(const Debug_visualizer&) = delete;
    Debug_visualizer& operator=(const Debug_visualizer&) = delete;
    Debug_visualizer(Debug_visualizer&&) = delete;
    Debug_visualizer& operator=(Debug_visualizer&&) = delete;

    // Update hotkey state (call once per frame)
    void update_input() noexcept;

    // Render debug visualization (call during present, after scene is complete)
    // Parameters:
    //   dc - Device context
    //   output_rtv - Render target (typically swapchain)
    //   nearscene_depth_srv - Nearscene depth buffer SRV
    //   farscene_depth_srv - Farscene depth buffer SRV (for dual-buffer mode)
    //   depth_dsv_readonly - Read-only DSV for stencil tests
    //   dsv_sample_count - Sample count of the DSV (for MSAA handling)
    //   nearscene_proj - Projection matrix for nearscene depth linearization
    //   farscene_proj - Projection matrix for farscene depth linearization
    //   render_width, render_height - Output dimensions
    void render(ID3D11DeviceContext4& dc,
                ID3D11RenderTargetView* output_rtv,
                ID3D11ShaderResourceView* nearscene_depth_srv,
                ID3D11ShaderResourceView* farscene_depth_srv,
                ID3D11DepthStencilView* depth_dsv_readonly,
                UINT dsv_sample_count,
                const glm::mat4& nearscene_proj,
                const glm::mat4& farscene_proj,
                std::uint32_t render_width,
                std::uint32_t render_height) noexcept;

    // ImGui settings panel
    void show_imgui() noexcept;

    // Configuration access
    Debug_visualizer_config& config() noexcept { return _config; }
    const Debug_visualizer_config& config() const noexcept { return _config; }

    // Check if active (for conditional rendering)
    bool is_active() const noexcept { return _config.mode != Debug_visualizer_mode::none; }

    // Update buffer source info for ImGui display
    void set_buffer_source_info(bool using_captured_depth, bool depth_capture_available,
                                 bool using_captured_stencil, bool stencil_capture_available,
                                 int depth_clear_count, int stencil_clear_count,
                                 bool capture_from_farscene, bool frame_swapped) noexcept {
        _using_captured_depth = using_captured_depth;
        _captured_depth_available = depth_capture_available;
        _using_captured_stencil = using_captured_stencil;
        _captured_stencil_available = stencil_capture_available;
        _depth_clear_count = depth_clear_count;
        _stencil_clear_count = stencil_clear_count;
        _capture_from_farscene = capture_from_farscene;
        _frame_swapped = frame_swapped;
    }

    // Update projection matrix info for diagnostics
    void set_projection_info(const glm::mat4& nearscene_proj,
                             const glm::mat4& farscene_proj) noexcept {
        _nearscene_proj_info = nearscene_proj;
        _farscene_proj_info = farscene_proj;
    }

private:
    // Initialization helpers
    void init_shaders(shader::Database& shaders) noexcept;
    void init_constant_buffers() noexcept;
    void init_blend_state() noexcept;
    void init_depth_stencil_states() noexcept;
    void init_sampler_state() noexcept;

    // Render helpers
    void render_depth(ID3D11DeviceContext4& dc,
                      ID3D11RenderTargetView* output_rtv,
                      ID3D11ShaderResourceView* nearscene_depth_srv,
                      ID3D11ShaderResourceView* farscene_depth_srv,
                      const glm::mat4& nearscene_proj,
                      const glm::mat4& farscene_proj,
                      std::uint32_t width, std::uint32_t height) noexcept;

    void render_stencil(ID3D11DeviceContext4& dc,
                        ID3D11RenderTargetView* output_rtv,
                        ID3D11DepthStencilView* depth_dsv_readonly,
                        UINT dsv_sample_count,
                        std::uint32_t width, std::uint32_t height) noexcept;

    void render_stencil_nonzero_pass(ID3D11DeviceContext4& dc) noexcept;
    void render_stencil_value_passes(ID3D11DeviceContext4& dc) noexcept;
    void render_stencil_bitmask_passes(ID3D11DeviceContext4& dc) noexcept;

    // Update constant buffer data
    void update_depth_constants(const glm::mat4& nearscene_proj,
                                const glm::mat4& farscene_proj) noexcept;
    void update_stencil_constants(const glm::vec4& color) noexcept;

    // Hotkey helpers
    bool key_just_pressed(std::uint32_t vk_code) noexcept;
    void cycle_mode() noexcept;

    // Get or create stencil test state for specific value
    ID3D11DepthStencilState* get_stencil_equal_state(std::uint8_t ref) noexcept;

    // Configuration
    Debug_visualizer_config _config;
    Debug_visualizer_mode _last_active_mode = Debug_visualizer_mode::depth_linear;

    // D3D11 device
    Com_ptr<ID3D11Device5> _device;

    // Shaders
    Com_ptr<ID3D11VertexShader> _vs;
    Com_ptr<ID3D11PixelShader> _depth_ps;
    Com_ptr<ID3D11PixelShader> _stencil_ps;

    // Constant buffers
    Com_ptr<ID3D11Buffer> _depth_cb;
    Com_ptr<ID3D11Buffer> _stencil_cb;
    Debug_depth_cb _depth_cb_data = {};
    Debug_stencil_cb _stencil_cb_data = {};

    // States
    Com_ptr<ID3D11BlendState> _overlay_blend_state;
    Com_ptr<ID3D11BlendState> _opaque_blend_state;
    Com_ptr<ID3D11SamplerState> _point_sampler;

    // Depth-stencil states for stencil testing
    Com_ptr<ID3D11DepthStencilState> _ds_disabled;        // No depth/stencil test
    Com_ptr<ID3D11DepthStencilState> _ds_stencil_notequal_zero;
    Com_ptr<ID3D11DepthStencilState> _ds_stencil_always;  // Debug: always pass
    std::array<Com_ptr<ID3D11DepthStencilState>, 8> _ds_stencil_bitmask;
    std::array<Com_ptr<ID3D11DepthStencilState>, 256> _ds_stencil_equal;  // Lazy-init

    // Input state for hotkey detection
    std::array<bool, 256> _key_states = {};

    // Track MSAA state for ImGui warning
    UINT _last_dsv_sample_count = 1;

    // Track buffer source for ImGui display
    bool _using_captured_stencil = false;
    bool _captured_stencil_available = false;
    bool _using_captured_depth = false;
    bool _captured_depth_available = false;
    int _depth_clear_count = 0;
    int _stencil_clear_count = 0;
    bool _capture_from_farscene = false;  // Which buffer was captured from
    bool _frame_swapped = false;          // Whether near/far were swapped this frame

    // Projection matrix info for diagnostics
    glm::mat4 _nearscene_proj_info = glm::mat4(0.0f);
    glm::mat4 _farscene_proj_info = glm::mat4(0.0f);
};

} // namespace sp::core

// debug_visualizer.fx
// Debug visualization shaders for depth buffer and stencil mask inspection.
//
// This file provides:
// - Depth visualization pixel shader with linear/log/raw modes
// - Stencil overlay pixel shader (solid color, stencil test determines visibility)
//
// NOTE: Uses postprocess vertex shader (main_vs from postprocess.fx) for fullscreen triangle.
//
// IMPORTANT D3D9/D3D11 CONSTRAINTS:
// - Stencil values CANNOT be sampled in shaders (only depth channel accessible via SRV)
// - Stencil visualization uses hardware stencil tests, not texture reads
// - Depth is non-linear (perspective z/w) and requires linearization

//------------------------------------------------------------------------------
// Depth Visualization
//------------------------------------------------------------------------------

Texture2D<float> depth_texture : register(t0);      // Nearscene (primary) depth
Texture2D<float> depth_texture_far : register(t1);  // Farscene depth (for dual-buffer mode)
SamplerState point_sampler : register(s0);

cbuffer DebugDepthConstants : register(b0)
{
    float2 depth_linearize_params;      // x = (far*near)/(far-near), y = far/(far-near) - nearscene
    float  max_depth_distance;          // Normalization range in world units
    uint   visualization_mode;          // 0=linear, 1=log, 2=raw
    float  log_scale_factor;            // Controls logarithmic curve steepness
    float  brightness;                  // Output brightness multiplier (for combined mode)
    float  sky_threshold;               // Depth values >= this are treated as sky
    uint   use_dual_buffers;            // If 1, sample both buffers and take min
    float3 near_color;                  // Color for near objects
    float  _padding2;
    float3 far_color;                   // Color for far objects
    float  _padding3;
    float3 sky_color;                   // Color for sky/cleared depth
    float  _padding4;
    float2 depth_linearize_params_far;  // Linearization params for farscene
    float2 _padding5;
};

struct Vs_output
{
    float2 texcoords : TEXCOORD;
    float4 positionPS : SV_Position;
};

// Linearize perspective depth to view-space Z distance.
// Standard perspective projection stores: z_ndc = (f*z - f*n) / (z*(f-n))
// This function inverts that to recover linear view-space Z.
//
// The constants are derived from the projection matrix:
//   params.x = (far * near) / (far - near) = -proj[3][2]
//   params.y = far / (far - near) = proj[2][2]
//
// Formula: linear_z = params.x / (params.y - raw_depth)
float LinearizeDepthWithParams(float raw_depth, float2 params)
{
    return params.x / (params.y - raw_depth);
}

float LinearizeDepth(float raw_depth)
{
    return LinearizeDepthWithParams(raw_depth, depth_linearize_params);
}

float LinearizeDepthFar(float raw_depth)
{
    return LinearizeDepthWithParams(raw_depth, depth_linearize_params_far);
}

// Depth visualization pixel shader
// Outputs grayscale based on depth, with near=white and far=black.
float4 depth_ps(Vs_output input) : SV_Target0
{
    float raw_depth_near = depth_texture.Sample(point_sampler, input.texcoords);
    float raw_depth_far = depth_texture_far.Sample(point_sampler, input.texcoords);

    // Get the linear depth from each buffer
    float linear_z_near = LinearizeDepth(raw_depth_near);
    float linear_z_far = LinearizeDepthFar(raw_depth_far);

    // Determine which depth to use
    float raw_depth = raw_depth_near;
    float linear_z = linear_z_near;

    if (use_dual_buffers == 1)
    {
        // Take the minimum (closest) linear depth from both buffers
        // But handle sky values specially - don't let sky override real geometry
        bool near_is_sky = (raw_depth_near >= sky_threshold);
        bool far_is_sky = (raw_depth_far >= sky_threshold);

        if (near_is_sky && far_is_sky)
        {
            // Both are sky - show sky color
            return float4(sky_color * brightness, 1.0);
        }
        else if (near_is_sky)
        {
            // Near is sky, use far
            linear_z = linear_z_far;
            raw_depth = raw_depth_far;
        }
        else if (far_is_sky)
        {
            // Far is sky, use near
            linear_z = linear_z_near;
            raw_depth = raw_depth_near;
        }
        else
        {
            // Both have geometry - take the closer one
            if (linear_z_near < linear_z_far)
            {
                linear_z = linear_z_near;
                raw_depth = raw_depth_near;
            }
            else
            {
                linear_z = linear_z_far;
                raw_depth = raw_depth_far;
            }
        }
    }
    else if (use_dual_buffers == 2)
    {
        // Farscene only mode - for debugging
        raw_depth = raw_depth_far;
        linear_z = linear_z_far;

        // Handle sky/cleared depth
        if (raw_depth >= sky_threshold)
        {
            return float4(sky_color * brightness, 1.0);
        }
    }
    else
    {
        // Single buffer mode - use nearscene only
        raw_depth = raw_depth_near;
        linear_z = linear_z_near;

        // Handle sky/cleared depth
        if (raw_depth >= sky_threshold)
        {
            return float4(sky_color * brightness, 1.0);
        }
    }

    float normalized;

    switch (visualization_mode)
    {
        case 0:  // Linear - uniform gradient from near to far
        {
            normalized = saturate(linear_z / max_depth_distance);
            break;
        }

        case 1:  // Logarithmic - better detail visibility at all distances
        {
            // Log scaling compresses far distances while expanding near distances.
            // This makes distant geometry more visible in large outdoor scenes.
            float log_depth = log(1.0 + linear_z * log_scale_factor);
            float log_max = log(1.0 + max_depth_distance * log_scale_factor);
            normalized = saturate(log_depth / log_max);
            break;
        }

        case 2:  // Raw - show actual buffer values without linearization
        default:
        {
            // Useful for debugging projection issues or understanding
            // how depth precision is distributed.
            normalized = raw_depth;
            break;
        }
    }

    // Lerp between near and far colors based on normalized depth
    // near_color for close objects, far_color for distant objects
    float3 depth_color = lerp(near_color, far_color, normalized) * brightness;

    return float4(depth_color, 1.0);
}

//------------------------------------------------------------------------------
// Stencil Visualization
//------------------------------------------------------------------------------

// The stencil shader is deliberately simple - it just outputs a solid color.
// The stencil TEST (configured via D3D11 depth-stencil state) determines
// which pixels pass and receive this color.
//
// This is the ONLY way to "read" stencil values in D3D9/D3D11 - the stencil
// portion of the depth-stencil texture is not accessible via shader sampling.

cbuffer DebugStencilConstants : register(b0)
{
    float4 overlay_color;  // RGBA color for this stencil pass
};

float4 stencil_ps(Vs_output input) : SV_Target0
{
    // Output solid color - stencil test determines where this appears
    return overlay_color;
}


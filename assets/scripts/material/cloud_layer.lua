
constant_buffer_bind = constant_buffer_bind_flag.ps
fail_safe_texture_index = 0

function make_constant_buffer(props)
   local cb = constant_buffer_builder.new([[
      float layer_scale;
      float layer_height;
      float scroll_angle;
      float scroll_speed;
      
      float cloud_threshold;
      float cloud_softness;
      float cloud_density;
      float detail_scale;
      
      float detail_strength;
      float edge_fade_start;
      float edge_fade_end;
      float height_fade_start;
      
      float3 cloud_color_lit;
      float lighting_wrap;
      
      float3 cloud_color_dark;
      float sun_color_influence;
      
      float horizon_fade_start;
      float horizon_fade_end;
      float cloud_brightness;
      float min_brightness;
      
      float near_fade_start;
      float near_fade_end;
      float world_scale;
      float _pad0;
   ]])

   cb:set("layer_scale", props:get_float("LayerScale", 0.5))
   cb:set("layer_height", props:get_float("LayerHeight", 100.0))
   cb:set("scroll_angle", props:get_float("ScrollAngle", 45.0))
   cb:set("scroll_speed", props:get_float("ScrollSpeed", 0.008))
   
   cb:set("cloud_threshold", props:get_float("CloudThreshold", 0.45))
   cb:set("cloud_softness", props:get_float("CloudSoftness", 0.25))
   cb:set("cloud_density", props:get_float("CloudDensity", 0.9))
   cb:set("detail_scale", props:get_float("DetailScale", 3.0))
   
   cb:set("detail_strength", props:get_float("DetailStrength", 0.3))
   cb:set("edge_fade_start", props:get_float("EdgeFadeStart", 8000.0))
   cb:set("edge_fade_end", props:get_float("EdgeFadeEnd", 11000.0))
   cb:set("height_fade_start", props:get_float("HeightFadeStart", 500.0))
   
   cb:set("cloud_color_lit", 
          props:get_float3("CloudColorLit", float3.new(1.0, 0.98, 0.95)))
   cb:set("lighting_wrap", props:get_float("LightingWrap", 0.4))
   
   cb:set("cloud_color_dark",
          props:get_float3("CloudColorDark", float3.new(0.6, 0.65, 0.75)))
   cb:set("sun_color_influence", props:get_float("SunColorInfluence", 0.3))
   
   cb:set("horizon_fade_start", props:get_float("HorizonFadeStart", 0.1))
   cb:set("horizon_fade_end", props:get_float("HorizonFadeEnd", 0.02))
   cb:set("cloud_brightness", props:get_float("CloudBrightness", 1.0))
   cb:set("min_brightness", props:get_float("MinBrightness", 0.3))
   
   cb:set("near_fade_start", props:get_float("NearFadeStart", 500.0))
   cb:set("near_fade_end", props:get_float("NearFadeEnd", 100.0))
   cb:set("world_scale", props:get_float("WorldScale", 0.0001))
   cb:set("_pad0", 0.0)

   return cb:complete()
end

function fill_resource_vec(props, resource_props, resources)
   resources:add(resource_props["CloudNoise"] or "$grey")
   resources:add(resource_props["CloudDetail"] or resource_props["CloudNoise"] or "$grey")
end

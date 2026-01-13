
constant_buffer_bind = constant_buffer_bind_flag.ps
fail_safe_texture_index = 0

function make_constant_buffer(props)
   local cb = constant_buffer_builder.new([[
      float blend_bottom;
      float blend_top;
   ]])

   cb:set("blend_bottom", props:get_float("BlendBottom", 0.0))
   cb:set("blend_top", props:get_float("BlendTop", 100.0))

   return cb:complete()
end

function fill_resource_vec(props, resource_props, resources)

   resources:add(resource_props["TextureA"] or "$white")
   resources:add(resource_props["TextureB"] or "$white")

end

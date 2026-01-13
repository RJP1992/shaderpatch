
#include "editor.hpp"

#include <algorithm>
#include <type_traits>

#include "../imgui/imgui.h"
#include "../imgui/imgui_stdlib.h"

#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <regex>
#include <unordered_map>

using namespace std::literals;

namespace sp::material {

    namespace {

        template<typename Type>
        constexpr auto property_datatype() noexcept -> ImGuiDataType
        {
            if constexpr (std::is_same_v<Type, glm::uint8>) return ImGuiDataType_U8;
            if constexpr (std::is_same_v<Type, glm::uint16>) return ImGuiDataType_U16;
            if constexpr (std::is_same_v<Type, glm::uint32>) return ImGuiDataType_U32;
            if constexpr (std::is_same_v<Type, glm::uint64>) return ImGuiDataType_U64;
            if constexpr (std::is_same_v<Type, glm::int8>) return ImGuiDataType_S8;
            if constexpr (std::is_same_v<Type, glm::int16>) return ImGuiDataType_S16;
            if constexpr (std::is_same_v<Type, glm::int32>) return ImGuiDataType_S32;
            if constexpr (std::is_same_v<Type, glm::int64>) return ImGuiDataType_S64;
            if constexpr (std::is_same_v<Type, float>) return ImGuiDataType_Float;
            if constexpr (std::is_same_v<Type, float>) return ImGuiDataType_Double;
        }

        template<typename Type>
        constexpr auto property_speed() noexcept -> float
        {
            if constexpr (std::is_integral_v<Type>) return 0.25f;
            if constexpr (std::is_floating_point_v<Type>) return 0.01f;
        }

        template<typename Type>
        struct Property_traits {
            inline constexpr static ImGuiDataType data_type = property_datatype<Type>();
            inline constexpr static float speed = property_speed<Type>();
            inline constexpr static int length = 1;
        };

        template<typename Type, std::size_t len>
        struct Property_traits<glm::vec<len, Type>> {
            inline constexpr static ImGuiDataType data_type = property_datatype<Type>();
            inline constexpr static float speed = property_speed<Type>();
            inline constexpr static int length = len;
        };

        constexpr auto v = Property_traits<float>::length;

        template<typename Var_type>
        void property_editor(const std::string& name, Material_var<Var_type>& var) noexcept
        {
            using Traits = Property_traits<Var_type>;

            ImGui::DragScalarN(name.c_str(), Traits::data_type, &var.value,
                Traits::length, Traits::speed, &var.min, &var.max);

            var.value = glm::clamp(var.value, var.min, var.max);
        }

        void property_editor(const std::string& name, Material_var<glm::vec3>& var) noexcept
        {
            if (name.find("Color") != name.npos) {
                ImGui::ColorEdit3(name.c_str(), &var.value[0], ImGuiColorEditFlags_Float);
            }
            else {
                using Traits = Property_traits<glm::vec3>;

                ImGui::DragScalarN(name.c_str(), Traits::data_type, &var.value,
                    Traits::length, Traits::speed, &var.min, &var.max);
            }

            var.value = glm::clamp(var.value, var.min, var.max);
        }

        void property_editor(const std::string& name, Material_var<bool>& var) noexcept
        {
            ImGui::Checkbox(name.c_str(), &var.value);
        }

        // ============================================================================
        // MTRL DUMPER - FLOAT FORMATTING
        // ============================================================================

        std::string format_float(float val) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(6) << val;
            std::string str = ss.str();

            size_t dot = str.find('.');
            if (dot != std::string::npos) {
                size_t last = str.find_last_not_of('0');
                if (last != std::string::npos && last > dot) {
                    str = str.substr(0, last + 1);
                }
                else if (last == dot) {
                    str = str.substr(0, dot + 2);
                }
            }
            return str;
        }

        // ============================================================================
        // MTRL DUMPER - LUA DEFAULT VALUE PARSING
        // ============================================================================

        struct DefaultValue {
            enum class Type { Float, Bool, Float3 };
            Type type;
            float f_val = 0.0f;
            bool b_val = false;
            glm::vec3 v3_val{ 0.0f };
        };

        using DefaultsMap = std::unordered_map<std::string, DefaultValue>;
        std::unordered_map<std::string, DefaultsMap> g_material_defaults_cache;

        DefaultsMap parse_lua_defaults(const std::filesystem::path& lua_path) {
            DefaultsMap defaults;

            std::ifstream file(lua_path);
            if (!file.is_open()) return defaults;

            std::string content((std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());

            // Match: props:get_float("PropName", value)
            std::regex float_regex(R"(props:get_float\s*\(\s*\"([^\"]+)\"\s*,\s*([0-9.\-]+)\s*\))");
            // Match: props:get_bool("PropName", true/false)
            std::regex bool_regex(R"(props:get_bool\s*\(\s*\"([^\"]+)\"\s*,\s*(true|false)\s*\))");
            // Match: props:get_float3("PropName", float3.new(x, y, z))
            std::regex float3_regex(R"(props:get_float3\s*\(\s*\"([^\"]+)\"\s*,\s*float3\.new\s*\(\s*([0-9.\-]+)\s*,\s*([0-9.\-]+)\s*,\s*([0-9.\-]+)\s*\)\s*\))");

            std::smatch match;
            std::string::const_iterator search_start(content.cbegin());

            // Parse float3 values
            while (std::regex_search(search_start, content.cend(), match, float3_regex)) {
                DefaultValue dv;
                dv.type = DefaultValue::Type::Float3;
                dv.v3_val.x = std::stof(match[2].str());
                dv.v3_val.y = std::stof(match[3].str());
                dv.v3_val.z = std::stof(match[4].str());
                defaults[match[1].str()] = dv;
                search_start = match.suffix().first;
            }

            // Parse float values
            search_start = content.cbegin();
            while (std::regex_search(search_start, content.cend(), match, float_regex)) {
                std::string prop_name = match[1].str();
                if (defaults.find(prop_name) == defaults.end()) { // Don't overwrite float3
                    DefaultValue dv;
                    dv.type = DefaultValue::Type::Float;
                    dv.f_val = std::stof(match[2].str());
                    defaults[prop_name] = dv;
                }
                search_start = match.suffix().first;
            }

            // Parse bool values
            search_start = content.cbegin();
            while (std::regex_search(search_start, content.cend(), match, bool_regex)) {
                DefaultValue dv;
                dv.type = DefaultValue::Type::Bool;
                dv.b_val = (match[2].str() == "true");
                defaults[match[1].str()] = dv;
                search_start = match.suffix().first;
            }

            return defaults;
        }

        const DefaultsMap& get_defaults_for_type(const std::string& material_type) {
            static DefaultsMap empty_map;

            auto it = g_material_defaults_cache.find(material_type);
            if (it != g_material_defaults_cache.end()) {
                return it->second;
            }

            // Try to load from Lua file
            std::filesystem::path lua_path = "data/shaderpatch/scripts/material/" + material_type + ".lua";
            if (std::filesystem::exists(lua_path)) {
                g_material_defaults_cache[material_type] = parse_lua_defaults(lua_path);
                return g_material_defaults_cache[material_type];
            }

            return empty_map;
        }

        // ============================================================================
        // MTRL DUMPER - VALUE COMPARISON
        // ============================================================================

        bool floats_equal(float a, float b, float epsilon = 0.0001f) {
            return std::abs(a - b) < epsilon;
        }

        bool is_default_value(const std::string& material_type, const std::string& prop_name,
            const Material_property::Value& value) {
            const auto& defaults = get_defaults_for_type(material_type);

            auto it = defaults.find(prop_name);
            if (it == defaults.end()) return false;

            const auto& def = it->second;

            return std::visit([&def](const auto& v) -> bool {
                using T = std::decay_t<decltype(v)>;

                if constexpr (std::is_same_v<T, Material_var<float>>) {
                    return def.type == DefaultValue::Type::Float && floats_equal(v.value, def.f_val);
                }
                else if constexpr (std::is_same_v<T, Material_var<bool>>) {
                    return def.type == DefaultValue::Type::Bool && v.value == def.b_val;
                }
                else if constexpr (std::is_same_v<T, Material_var<glm::vec3>>) {
                    return def.type == DefaultValue::Type::Float3 &&
                        floats_equal(v.value.x, def.v3_val.x) &&
                        floats_equal(v.value.y, def.v3_val.y) &&
                        floats_equal(v.value.z, def.v3_val.z);
                }
                else 
                    return false;
                }, value);
        }

        // ============================================================================
        // MTRL DUMPER - VALUE TO STRING
        // ============================================================================

        std::string property_to_string(const Material_property::Value& value) {
            return std::visit([](const auto& v) -> std::string {
                using T = std::decay_t<decltype(v)>;

                if constexpr (std::is_same_v<T, Material_var<float>>) {
                    return format_float(v.value);
                }
                else if constexpr (std::is_same_v<T, Material_var<bool>>) {
                    return v.value ? "yes" : "no";
                }
                else if constexpr (std::is_same_v<T, Material_var<glm::vec2>>) {
                    return format_float(v.value.x) + ", " + format_float(v.value.y);
                }
                else if constexpr (std::is_same_v<T, Material_var<glm::vec3>>) {
                    return format_float(v.value.x) + ", " + format_float(v.value.y) + ", " + format_float(v.value.z);
                }
                else if constexpr (std::is_same_v<T, Material_var<glm::vec4>>) {
                    return format_float(v.value.x) + ", " + format_float(v.value.y) + ", " +
                        format_float(v.value.z) + ", " + format_float(v.value.w);
                }
                else if constexpr (std::is_same_v<T, Material_var<std::int32_t>>) {
                    return std::to_string(v.value);
                }
                else if constexpr (std::is_same_v<T, Material_var<std::uint32_t>>) {
                    return std::to_string(v.value);
                }
                else if constexpr (std::is_same_v<T, Material_var<glm::ivec2>>) {
                    return std::to_string(v.value.x) + ", " + std::to_string(v.value.y);
                }
                else if constexpr (std::is_same_v<T, Material_var<glm::ivec3>>) {
                    return std::to_string(v.value.x) + ", " + std::to_string(v.value.y) + ", " + std::to_string(v.value.z);
                }
                else if constexpr (std::is_same_v<T, Material_var<glm::ivec4>>) {
                    return std::to_string(v.value.x) + ", " + std::to_string(v.value.y) + ", " +
                        std::to_string(v.value.z) + ", " + std::to_string(v.value.w);
                }
                else if constexpr (std::is_same_v<T, Material_var<glm::uvec2>>) {
                    return std::to_string(v.value.x) + ", " + std::to_string(v.value.y);
                }
                else if constexpr (std::is_same_v<T, Material_var<glm::uvec3>>) {
                    return std::to_string(v.value.x) + ", " + std::to_string(v.value.y) + ", " + std::to_string(v.value.z);
                }
                else if constexpr (std::is_same_v<T, Material_var<glm::uvec4>>) {
                    return std::to_string(v.value.x) + ", " + std::to_string(v.value.y) + ", " +
                        std::to_string(v.value.z) + ", " + std::to_string(v.value.w);
                }
                }, value);
        }

        // ============================================================================
        // MTRL DUMPER - MAIN DUMP FUNCTION
        // ============================================================================

        void dump_material_to_mtrl(const Material& material) {
            std::filesystem::path output_dir = "data/shaderpatch/material_dumps";
            std::filesystem::create_directories(output_dir);

            // Sanitize filename
            std::string filename = material.name;
            for (char& c : filename) {
                if (c == '/' || c == '\\' || c == ':' || c == '*' ||
                    c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                    c = '_';
                }
            }

            std::ofstream out(output_dir / (filename + ".mtrl"), std::ios::binary);
            if (!out.is_open()) return;

            // Write Type
            out << "Type: " << material.type << "\n";

            // Collect non-default properties
            std::vector<std::pair<std::string, std::string>> non_default_props;
            for (const auto& prop : material.properties) {
                if (!is_default_value(material.type, prop.name, prop.value)) {
                    non_default_props.emplace_back(prop.name, property_to_string(prop.value));
                }
            }

            // Write Material section if there are non-default properties
            if (!non_default_props.empty()) {
                out << "Material:\r\n";
                for (const auto& [name, value] : non_default_props) {
                    out << "  " << name << ": " << value << "\n";
                }
            }

            // Collect non-empty textures
            std::vector<std::pair<std::string, std::string>> textures;
            for (const auto& [key, value] : material.resource_properties) {
                if (!value.empty()) {
                    textures.emplace_back(key, value);
                }
            }

            // Write Textures section if there are any
            if (!textures.empty()) {
                out << "Textures:\r\n";
                for (const auto& [name, value] : textures) {
                    out << "  " << name << ": " << value << "\n";
                }
            }
        }

        // ============================================================================
        // MATERIAL EDITOR
        // ============================================================================

        void material_editor(Factory& factory, Material& material) noexcept
        {
            if (!material.properties.empty() && ImGui::TreeNode("Properties")) {
                for (auto& prop : material.properties) {
                    std::visit([&](auto& value) { property_editor(prop.name, value); },
                        prop.value);
                }

                ImGui::TreePop();
            }

            if (!material.resource_properties.empty() &&
                ImGui::TreeNode("Shader Resources")) {
                for (auto& [key, value] : material.resource_properties) {
                    if (ImGui::BeginCombo(key.c_str(), value.data(), ImGuiComboFlags_HeightLargest)) {
                        const core::Shader_resource_database::Imgui_pick_result picked =
                            factory.shader_resource_database().imgui_resource_picker();

                        if (picked.srv) {
                            value = picked.name;
                        }

                        ImGui::EndCombo();
                    }
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Advanced")) {
                ImGui::Text("Type: %s", material.type.c_str());
                ImGui::Text("Overridden Rendertype: %s",
                    to_string(material.overridden_rendertype).c_str());

                ImGui::TreePop();
            }

            if (ImGui::Button("Dump .mtrl")) {
                dump_material_to_mtrl(material);
            }

            factory.update_material(material);
        }
    }

    void show_editor(Factory& factory,
        const std::vector<std::unique_ptr<Material>>& materials) noexcept
    {
        if (ImGui::Begin("Materials")) {
            for (auto& material : materials) {
                if (ImGui::TreeNode(material->name.c_str())) {
                    material_editor(factory, *material);
                    ImGui::TreePop();
                }
            }
        }

        ImGui::End();
    }
}
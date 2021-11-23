/*
   Copyright 2021 Nora Beda and contributors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "pch.h"
#include "shader.h"
#include "renderer.h"
#include "util.h"
#include "pipeline.h"
#include <shaderc/shaderc.hpp>
#include <spirv_cross.hpp>
namespace vkrollercoaster {
    VkShaderStageFlagBits shader::get_stage_flags(shader_stage stage) {
        switch (stage) {
        case shader_stage::vertex:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case shader_stage::fragment:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        case shader_stage::geometry:
            return VK_SHADER_STAGE_GEOMETRY_BIT;
        case shader_stage::compute:
            return VK_SHADER_STAGE_COMPUTE_BIT;
        default:
            throw std::runtime_error("invalid shader stage");
        }
    }
    static std::map<std::string, shader_language> language_map = {
        { ".glsl", shader_language::glsl },
        { ".hlsl", shader_language::hlsl }
    };
    static shader_language determine_language(const fs::path& path) {
        auto extension = path.extension().string();
        if (language_map.find(extension) == language_map.end()) {
            throw std::runtime_error("invalid shader extension: " + extension);
        }
        return language_map[extension];
    }
    shader::shader(const fs::path& path) : shader(path, determine_language(path)) { }
    shader::shader(const fs::path& path, shader_language language) {
        this->m_path = path;
        this->m_language = language;
        renderer::add_ref();
        this->create();
    }
    shader::~shader() {
        this->destroy();
        renderer::remove_ref();
    }
    void shader::reload() {
        for (auto _pipeline : this->m_dependents) {
            _pipeline->destroy_pipeline();
            _pipeline->destroy_descriptor_sets();
        }
        this->destroy();
        this->create();
        for (auto _pipeline : this->m_dependents) {
            _pipeline->create_descriptor_sets();
            _pipeline->create_pipeline();
        }
    }
    void shader::create() {
        std::map<shader_stage, std::vector<uint32_t>> spirv;
        this->compile(spirv);
        VkDevice device = renderer::get_device();
        for (const auto& [stage, data] : spirv) {
            this->reflect(data, stage);
            VkShaderModuleCreateInfo module_create_info;
            util::zero(module_create_info);
            module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            module_create_info.codeSize = data.size() * sizeof(uint32_t);
            module_create_info.pCode = data.data();
            auto& stage_create_info = this->m_shader_data.emplace_back();
            util::zero(stage_create_info);
            stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            if (vkCreateShaderModule(device, &module_create_info, nullptr, &stage_create_info.module) != VK_SUCCESS) {
                throw std::runtime_error("could not create shader module!");
            }
            stage_create_info.pName = "main";
            stage_create_info.stage = get_stage_flags(stage);
        }
    }
    static std::map<std::string, shader_stage> stage_map = {
        { "vertex", shader_stage::vertex },
        { "fragment", shader_stage::fragment },
        { "pixel", shader_stage::fragment },
        { "geometry", shader_stage::geometry },
        { "compute", shader_stage::compute }
    };
    void shader::compile(std::map<shader_stage, std::vector<uint32_t>>& spirv) {
        // todo: shader cache
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        shaderc_source_language source_language;
        switch (this->m_language) {
        case shader_language::glsl:
            source_language = shaderc_source_language_glsl;
            break;
        case shader_language::hlsl:
            source_language = shaderc_source_language_hlsl;
            break;
        default:
            throw std::runtime_error("invalid shader language!");
        }
        options.SetSourceLanguage(source_language);
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
        options.SetWarningsAsErrors();
        options.SetGenerateDebugInfo();
        std::map<shader_stage, std::stringstream> sources;
        {
            std::stringstream file_data(util::read_file(this->m_path));
            std::string line;
            std::optional<shader_stage> current_stage;
            std::string stage_switch = "#stage ";
            while (std::getline(file_data, line)) {
                if (line.substr(0, stage_switch.length()) == stage_switch) {
                    std::string stage_string = line.substr(stage_switch.length());
                    if (stage_map.find(stage_string) == stage_map.end()) {
                        throw std::runtime_error(this->m_path.string() + ": invalid shader stage: " + stage_string);
                    }
                    current_stage = stage_map[stage_string];
                } else {
                    if (!current_stage.has_value()) {
                        spdlog::warn("{0}: no stage specified - assuming compute", this->m_path.string());
                        current_stage = shader_stage::compute;
                    }
                    shader_stage stage = *current_stage;
                    sources[stage] << line << '\n';
                }
            }
        }
        for (const auto& [stage, stream] : sources) {
            auto source = stream.str();
            shaderc_shader_kind shaderc_stage;
            switch (stage) {
            case shader_stage::vertex:
                shaderc_stage = shaderc_vertex_shader;
                break;
            case shader_stage::fragment:
                shaderc_stage = shaderc_fragment_shader;
                break;
            case shader_stage::geometry:
                shaderc_stage = shaderc_geometry_shader;
                break;
            case shader_stage::compute:
                shaderc_stage = shaderc_compute_shader;
                break;
            default:
                throw std::runtime_error("invalid shader stage!");
            }
            std::string path = this->m_path.string();
            auto result = compiler.CompileGlslToSpv(source, shaderc_stage, path.c_str());
            if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
                throw std::runtime_error("could not compile shader: " + result.GetErrorMessage());
            }
            spirv[stage] = std::vector<uint32_t>(result.cbegin(), result.cend());
        }
    }
    static std::map<spirv_cross::TypeID, size_t> found_types;
    static size_t get_type_size(const spirv_cross::SPIRType& type, const spirv_cross::Compiler& compiler) {
        using BaseType = spirv_cross::SPIRType::BaseType;
        switch (type.basetype) {
        case BaseType::Boolean:
        case BaseType::Char:
            return 1;
        case BaseType::Float:
        case BaseType::Int:
        case BaseType::UInt:
        case BaseType::SampledImage:
        case BaseType::Sampler:
            return 4;
        case BaseType::Int64:
        case BaseType::Double:
        case BaseType::UInt64:
            return 8;
        case BaseType::Struct:
            return compiler.get_declared_struct_size(type);
        default:
            throw std::runtime_error("invalid base type");
        }
        return 0;
    }
    static size_t get_type(const spirv_cross::Compiler& compiler, spirv_cross::TypeID id, spirv_cross::TypeID parent, uint32_t member_index, std::vector<shader_type>& types) {
        if (found_types.find(id) != found_types.end()) {
            return found_types[id];
        }
        size_t type_index = types.size();
        found_types[id] = type_index;
        auto& type = types.emplace_back();
        const auto& spirv_type = compiler.get_type(id);
        type.name = compiler.get_name(id);
        type.size = get_type_size(spirv_type, compiler) * spirv_type.columns;
        if (spirv_type.array.empty()) {
            type.array_size = 1;
            type.array_stride = 0;
        } else {
            type.array_size = spirv_type.array[0];
            if (parent) {
                const auto& parent_type = compiler.get_type(parent);
                type.array_stride = compiler.type_struct_member_array_stride(parent_type, member_index);
            } else {
                type.array_stride = type.size;
            }
        }
        for (uint32_t i = 0; i < spirv_type.member_types.size(); i++) {
            std::string name = compiler.get_member_name(spirv_type.self, i);
            auto& field = type.fields[name];
            field.offset = compiler.type_struct_member_offset(spirv_type, i);
            field.type = get_type(compiler, spirv_type.member_types[i], id, i, types);
        }
        return type_index;
    }
    void shader::reflect(const std::vector<uint32_t>& spirv, shader_stage stage) {
        spirv_cross::Compiler compiler(std::move(spirv));
        auto resources = compiler.get_shader_resources();
        for (const auto& resource : resources.uniform_buffers) {
            uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            auto& resource_desc = this->m_reflection_data.resources[set][binding];
            resource_desc.name = resource.name;
            resource_desc.resource_type = shader_resource_type::uniformbuffer;
            resource_desc.stage = stage;
            resource_desc.type = get_type(compiler, resource.type_id, spirv_cross::TypeID(), 0, this->m_reflection_data.types);
        }
        for (const auto& resource : resources.storage_buffers) {
            uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            auto& resource_desc = this->m_reflection_data.resources[set][binding];
            resource_desc.name = resource.name;
            resource_desc.resource_type = shader_resource_type::storagebuffer;
            resource_desc.stage = stage;
            resource_desc.type = get_type(compiler, resource.type_id, spirv_cross::TypeID(), 0, this->m_reflection_data.types);
        }
        for (const auto& resource : resources.sampled_images) {
            uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            auto& resource_desc = this->m_reflection_data.resources[set][binding];
            resource_desc.name = resource.name;
            resource_desc.resource_type = shader_resource_type::sampledimage;
            resource_desc.stage = stage;
            resource_desc.type = get_type(compiler, resource.type_id, spirv_cross::TypeID(), 0, this->m_reflection_data.types);
        }
        for (const auto& resource : resources.push_constant_buffers) {
            uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
            uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            auto& resource_desc = this->m_reflection_data.resources[set][binding];
            resource_desc.name = resource.name;
            resource_desc.resource_type = shader_resource_type::pushconstantbuffer;
            resource_desc.stage = stage;
            resource_desc.type = get_type(compiler, resource.type_id, spirv_cross::TypeID(), 0, this->m_reflection_data.types);
        }
        found_types.clear();
    }
    void shader::destroy() {
        VkDevice device = renderer::get_device();
        for (const auto& stage : this->m_shader_data) {
            vkDestroyShaderModule(device, stage.module, nullptr);
        }
        this->m_shader_data.clear();
    }
    size_t shader_type::find_offset(const std::string& field_name, const shader_reflection_data& base_data) const {
        size_t separator_pos = field_name.find('.');
        std::string name, subname;
        if (separator_pos != std::string::npos) {
            name = field_name.substr(0, separator_pos);
            subname = field_name.substr(separator_pos + 1);
            if (subname.empty()) {
                throw std::runtime_error("invalid field name");
            }
        } else {
            name = field_name;
        }
        int32_t index = -1;
        size_t open_bracket = name.find('[');
        if (open_bracket != std::string::npos) {
            size_t close_bracket = name.find(']');
            if (close_bracket <= open_bracket + 1 || close_bracket >= name.length() || close_bracket < name.length() - 1) {
                throw std::runtime_error("invalid index operator call");
            }
            size_t index_start = open_bracket + 1;
            std::string index_string = name.substr(index_start, close_bracket - index_start);
            name = name.substr(0, open_bracket);
            index = atoi(index_string.c_str());
        }
        if (this->fields.find(name) == this->fields.end()) {
            throw std::runtime_error(name + " is not the name of a field");
        }
        const auto& field = this->fields.find(name)->second;
        if (index != -1 && base_data.types[field.type].array_stride == 0) {
            throw std::runtime_error("attempted to index into a non-array field");
        }
        if (index == -1) {
            index = 0;
        }
        size_t offset = field.offset + (index * base_data.types[field.type].array_stride);
        if (subname.empty()) {
            return offset;
        } else {
            return offset + base_data.types[field.type].find_offset(subname, base_data);
        }
    }
    bool shader_reflection_data::find_resource(const std::string& name, uint32_t& set, uint32_t& binding) const {
        for (const auto& [current_set, resources] : this->resources) {
            for (const auto& [current_binding, resource] : resources) {
                if (resource.name == name) {
                    set = current_set;
                    binding = current_binding;
                    return true;
                }
            }
        }
        return false;
    }
}
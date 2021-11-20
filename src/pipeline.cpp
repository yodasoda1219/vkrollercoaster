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
#include "pipeline.h"
#include "renderer.h"
#include "util.h"
namespace vkrollercoaster {
    pipeline::pipeline(std::shared_ptr<swapchain> _swapchain, std::shared_ptr<shader> _shader, const vertex_input_data& vertex_inputs) {
        this->m_swapchain = _swapchain;
        this->m_shader = _shader;
        this->m_vertex_input_data = vertex_inputs;
        renderer::add_ref();
        this->create();
        this->m_shader->m_dependents.insert(this);
        this->m_swapchain->m_dependents.insert(this);
    }
    pipeline::~pipeline() {
        this->m_swapchain->m_dependents.erase(this);
        this->m_shader->m_dependents.erase(this);
        this->destroy();
        renderer::remove_ref();
    }
    void pipeline::create() {
        VkDevice device = renderer::get_device();
        VkExtent2D swapchain_extent = this->m_swapchain->get_extent();
        VkPipelineVertexInputStateCreateInfo vertex_input_info;
        util::zero(vertex_input_info);
        vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        std::vector<VkVertexInputAttributeDescription> attributes;
        VkVertexInputBindingDescription binding;
        util::zero(binding);
        binding.binding = 0;
        binding.stride = this->m_vertex_input_data.stride;
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        vertex_input_info.vertexBindingDescriptionCount = 1;
        vertex_input_info.pVertexBindingDescriptions = &binding;
        for (uint32_t i = 0; i < this->m_vertex_input_data.attributes.size(); i++) {
            const auto& attribute = this->m_vertex_input_data.attributes[i];
            auto& attribute_desc = attributes.emplace_back();
            util::zero(attribute_desc);
            attribute_desc.binding = 0;
            attribute_desc.location = i;
            attribute_desc.offset = attribute.offset;
            switch (attribute.type) {
            case vertex_attribute_type::FLOAT:
                attribute_desc.format = VK_FORMAT_R32_SFLOAT;
                break;
            case vertex_attribute_type::INT:
                attribute_desc.format = VK_FORMAT_R32_SINT;
                break;
            case vertex_attribute_type::VEC2:
                attribute_desc.format = VK_FORMAT_R32G32_SFLOAT;
                break;
            case vertex_attribute_type::IVEC2:
                attribute_desc.format = VK_FORMAT_R32G32_SINT;
                break;
            case vertex_attribute_type::VEC3:
                attribute_desc.format = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            case vertex_attribute_type::IVEC3:
                attribute_desc.format = VK_FORMAT_R32G32B32_SINT;
                break;
            case vertex_attribute_type::VEC4:
                attribute_desc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
            case vertex_attribute_type::IVEC4:
                attribute_desc.format = VK_FORMAT_R32G32B32A32_SINT;
                break;
            default:
                throw std::runtime_error("invalid vertex attribute type!");
            }
        }
        if (!attributes.empty()) {
            vertex_input_info.vertexAttributeDescriptionCount = attributes.size();
            vertex_input_info.pVertexAttributeDescriptions = attributes.data();
        }
        VkPipelineInputAssemblyStateCreateInfo input_assembly;
        util::zero(input_assembly);
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly.primitiveRestartEnable = false;
        util::zero(this->m_viewport);
        this->m_viewport.x = this->m_viewport.y = 0.f;
        this->m_viewport.width = (float)swapchain_extent.width;
        this->m_viewport.height = (float)swapchain_extent.height;
        this->m_viewport.minDepth = 0.f;
        this->m_viewport.maxDepth = 1.f;
        util::zero(this->m_scissor);
        this->m_scissor.extent = swapchain_extent;
        VkPipelineViewportStateCreateInfo viewport_state;
        util::zero(viewport_state);
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &this->m_viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &this->m_scissor;
        VkPipelineRasterizationStateCreateInfo rasterizer;
        util::zero(rasterizer);
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = false;
        rasterizer.rasterizerDiscardEnable = false;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = false;
        VkPipelineMultisampleStateCreateInfo multisampling;
        util::zero(multisampling);
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = false;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState color_blend_attachment;
        util::zero(color_blend_attachment);
        color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        color_blend_attachment.blendEnable = true;
        color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo color_blending;
        util::zero(color_blending);
        color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.logicOpEnable = false;
        color_blending.attachmentCount = 1;
        color_blending.pAttachments = &color_blend_attachment;
        std::vector<VkDynamicState> dynamic_states = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_LINE_WIDTH
        };
        VkPipelineDynamicStateCreateInfo dynamic_state;
        util::zero(dynamic_state);
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = dynamic_states.size();
        dynamic_state.pDynamicStates = dynamic_states.data();
        VkPipelineLayoutCreateInfo layout_create_info;
        util::zero(layout_create_info);
        layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        // todo: descriptor sets and whatnot
        if (vkCreatePipelineLayout(device, &layout_create_info, nullptr, &this->m_layout) != VK_SUCCESS) {
            throw std::runtime_error("could not create pipeline layout!");
        }
        // todo: create pipeline
    }
    void pipeline::destroy() {
        VkDevice device = renderer::get_device();
        // todo: destroy pipeline
        vkDestroyPipelineLayout(device, this->m_layout, nullptr);
    }
}
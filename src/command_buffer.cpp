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
#include "command_buffer.h"
#define EXPOSE_RENDERER_INTERNALS
#include "renderer.h"
#include "util.h"
namespace vkrollercoaster {
    command_buffer::~command_buffer() {
        VkDevice device = renderer::get_device();
        vkFreeCommandBuffers(device, this->m_pool, 1, &this->m_buffer);
        renderer::remove_ref();
    }
    void command_buffer::begin() {
        VkCommandBufferBeginInfo begin_info;
        util::zero(begin_info);
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (this->m_single_time) {
            begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        }
        if (vkBeginCommandBuffer(this->m_buffer, &begin_info) != VK_SUCCESS) {
            throw std::runtime_error("could not begin recording of command buffer!");
        }
    }
    void command_buffer::end() {
        if (vkEndCommandBuffer(this->m_buffer) != VK_SUCCESS) {
            throw std::runtime_error("could not end recording of command buffer!");
        }
    }
    // todo: more parameters i guess?
    void command_buffer::submit() {
        VkDevice device = renderer::get_device();
        VkSubmitInfo submit_info;
        util::zero(submit_info);
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &this->m_buffer;
        VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkFence fence = nullptr;
        if (this->m_render) {
            size_t current_frame = renderer::get_current_frame();
            const auto& frame_sync_objects = renderer::get_sync_objects(current_frame);
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &frame_sync_objects.image_available_semaphore;
            submit_info.pWaitDstStageMask = wait_stages;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &frame_sync_objects.render_finished_semaphore;
            fence = frame_sync_objects.fence;
        } else {
            VkFenceCreateInfo fence_create_info;
            util::zero(fence_create_info);
            fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (vkCreateFence(device, &fence_create_info, nullptr, &fence) != VK_SUCCESS) {
                throw std::runtime_error("could not create fence for syncing!");
            }
        }
        vkQueueSubmit(this->m_queue, 1, &submit_info, fence);
        if (!this->m_render) {
            vkWaitForFences(device, 1, &fence, true, std::numeric_limits<uint64_t>::max());
            vkDestroyFence(device, fence, nullptr);
        }
    }
    void command_buffer::reset() {
        vkQueueWaitIdle(this->m_queue);
        vkResetCommandBuffer(this->m_buffer, 0);
    }
    command_buffer::command_buffer(VkCommandPool command_pool, VkQueue queue, bool single_time, bool render) {
        this->m_single_time = single_time;
        this->m_render = render;
        this->m_pool = command_pool;
        this->m_queue = queue;
        renderer::add_ref();
        VkCommandBufferAllocateInfo alloc_info;
        util::zero(alloc_info);
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandBufferCount = 1;
        alloc_info.commandPool = this->m_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        VkDevice device = renderer::get_device();
        if (vkAllocateCommandBuffers(device, &alloc_info, &this->m_buffer) != VK_SUCCESS) {
            throw std::runtime_error("could not allocate command buffer!");
        }
    }
    void command_buffer::begin_render_pass(ref<swapchain> swap_chain, const glm::vec4& clear_color, size_t image_index) {
        VkRenderPassBeginInfo begin_info;
        util::zero(begin_info);
        begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin_info.renderPass = swap_chain->get_render_pass();
        const auto& swapchain_images = swap_chain->get_swapchain_images();
        begin_info.framebuffer = swapchain_images[image_index].framebuffer;
        begin_info.renderArea.offset = { 0, 0 };
        begin_info.renderArea.extent = swap_chain->get_extent();
        std::array<VkClearValue, 2> clear_values;
        util::zero(clear_values.data(), clear_values.size() * sizeof(VkClearValue));
        memcpy(clear_values[0].color.float32, &clear_color, sizeof(glm::vec4));
        clear_values[1].depthStencil = { 1.f, 0 };
        begin_info.clearValueCount = clear_values.size();
        begin_info.pClearValues = clear_values.data();
        vkCmdBeginRenderPass(this->m_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
    }
    void command_buffer::end_render_pass() {
        vkCmdEndRenderPass(this->m_buffer);
    }
}
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
#include "menus.h"
#include "../application.h"
namespace vkrollercoaster {
    static viewport* viewport_instance = nullptr;
    ref<viewport> viewport::get_instance() {
        return viewport_instance;
    }

    viewport::viewport() {
        if (viewport_instance) {
            throw std::runtime_error("cannot have more than 1 viewport window!");
        }
        viewport_instance = this;

        framebuffer_spec spec;

        // let's initialize this to the size of the swapchain
        ref<swapchain> swap_chain = application::get_swapchain();
        VkExtent2D swapchain_extent = swap_chain->get_extent();
        spec.width = swapchain_extent.width;
        spec.height = swapchain_extent.height;

        // we want both a color and depth attachment
        spec.requested_attachments[framebuffer_attachment_type::color] = VK_FORMAT_R8G8B8A8_UNORM;
        spec.requested_attachments[framebuffer_attachment_type::depth_stencil] = VK_FORMAT_D32_SFLOAT;

        this->m_framebuffer = ref<framebuffer>::create(spec);

        // create texture for displaying
        this->m_color_attachment = ref<texture>::create(this->m_framebuffer->get_attachment(framebuffer_attachment_type::color));
    }
    
    viewport::~viewport() {
        viewport_instance = nullptr;
    }

    void viewport::update() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::Begin("Viewport", &this->m_open);
        ImGui::PopStyleVar();

        VkExtent2D framebuffer_extent = this->m_framebuffer->get_extent();
        float aspect_ratio = (float)framebuffer_extent.width / (float)framebuffer_extent.height;
        ImVec2 available_region = ImGui::GetContentRegionAvail();
        float window_aspect_ratio = available_region.x / available_region.y;

        ImVec2 cursor_pos = ImVec2(0.f, 0.f);
        if (window_aspect_ratio > 1.f) {
            cursor_pos.x = available_region.x * (1.f - aspect_ratio / window_aspect_ratio) / 2.f;
        } else {
            cursor_pos.y = available_region.y * (1.f - window_aspect_ratio / aspect_ratio) / 2.f;
        }
        ImGui::SetCursorPos(cursor_pos);

        ImVec2 image_size;
        image_size.x = available_region.x - cursor_pos.x * 2.f;
        image_size.y = available_region.y - cursor_pos.y * 2.f;
        ImGui::Image(this->m_color_attachment->get_imgui_id(), image_size);

        ImGui::End();
    }
}
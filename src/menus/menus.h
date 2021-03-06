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

#pragma once
#include "../imgui_controller.h"
#include "../framebuffer.h"
#include "texture.h"
namespace vkrollercoaster {
    class inspector : public menu {
    public:
        virtual ~inspector() override;
        virtual std::string get_title() override { return "Inspector"; }
        virtual void update() override;
    };
    class renderer_info : public menu {
    public:
        virtual std::string get_title() override { return "Renderer info"; }
        virtual void update() override;
    };
    class viewport : public menu {
    public:
        static ref<viewport> get_instance();
        viewport();
        virtual ~viewport() override;
        virtual std::string get_title() override { return "Viewport"; }
        virtual void update() override;
        ref<framebuffer> get_framebuffer() { return this->m_framebuffer; }

    private:
        void update_framebuffer_size();
        void update_color_attachment();
        ref<framebuffer> m_framebuffer;
        ref<texture> m_color_attachment;
        ref<texture> m_previous_color_attachment;
    };
} // namespace vkrollercoaster
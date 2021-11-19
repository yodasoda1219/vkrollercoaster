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
#include "window.h"
namespace vkrollercoaster {
    class renderer {
    public:
        static void init(std::shared_ptr<window> _window);
        static void shutdown();
        static VkInstance get_instance();
        static VkPhysicalDevice get_physical_device();
        static VkDevice get_device();
        static VkQueue get_graphics_queue();
    private:
        renderer() = default;
    };
}
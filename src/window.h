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
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
namespace vkrollercoaster {
    class swapchain;
    class window : public ref_counted {
    public:
        static void init();
        static void shutdown();
        static void poll();
        static double get_time();
        window(int32_t width, int32_t height, const std::string& title);
        ~window();
        window(const window&) = delete;
        window& operator=(const window&) = delete;
        bool should_close() const;
        GLFWwindow* get() const { return this->m_window; }
        void get_size(int32_t* width, int32_t* height);
        float get_aspect_ratio();

    private:
        static void glfw_resize_callback(GLFWwindow* glfw_window, int32_t width, int32_t height);
        GLFWwindow* m_window;
        std::unordered_set<swapchain*> m_swapchains;
        friend class swapchain;
    };
} // namespace vkrollercoaster
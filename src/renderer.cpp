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
#define EXPOSE_RENDERER_INTERNALS
#include "renderer.h"
#include "util.h"
#include "components.h"
#include "allocator.h"
namespace vkrollercoaster {
    static struct {
        // extensions and layers
        std::set<std::string> instance_extensions, device_extensions, layer_names;

        // vulkan data
        VkInstance instance = nullptr;
        VkDebugUtilsMessengerEXT debug_messenger = nullptr;
        VkPhysicalDevice physical_device = nullptr;
        VkDevice device = nullptr;
        VkQueue graphics_queue = nullptr;
        VkQueue compute_queue = nullptr;
        VkDescriptorPool descriptor_pool = nullptr;
        VkCommandPool graphics_command_pool = nullptr;
        std::array<sync_objects, renderer::max_frame_count> frame_sync_objects;
        size_t current_frame = 0;
        uint32_t vulkan_version = 0;

        // core graphics objects
        ref<texture> white_texture;
        ref<uniform_buffer> camera_buffer;

        // current skybox
        ref<skybox> _skybox;

        // temp
        ref<model> track_model;

        // ref counting
        uint32_t ref_count = 0;
        bool should_shutdown = false;
    } renderer_data;

    static VKAPI_ATTR VkBool32 VKAPI_CALL validation_layer_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
        VkDebugUtilsMessageTypeFlagsEXT message_type,
        const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data) {
        std::string message = "validation layer: " + std::string(callback_data->pMessage);
        switch (message_severity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            spdlog::warn(message);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            spdlog::error(message);
            throw std::runtime_error("vulkan error encountered");
        default:
            // not important enough to show
            break;
        };
        return VK_FALSE;
    }

    static bool check_layer_availability(const std::string& layer_name) {
        uint32_t layer_count;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        std::vector<VkLayerProperties> available_layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
        bool layer_found = false;
        for (const auto& layer : available_layers) {
            if (layer_name == layer.layerName) {
                layer_found = true;
                break;
            }
        }
        if (!layer_found) {
            return false;
        }
        return true;
    }

    void renderer::add_layer(const std::string& name) {
        if (renderer_data.layer_names.find(name) != renderer_data.layer_names.end()) {
            return;
        }
        if (!check_layer_availability(name)) {
            throw std::runtime_error("attempted to add unsupported layer!");
        }
        renderer_data.layer_names.insert(name);
    }

    void renderer::add_instance_extension(const std::string& name) {
        if (renderer_data.instance_extensions.find(name) !=
            renderer_data.instance_extensions.end()) {
            return;
        }
        uint32_t extension_count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> extensions(extension_count);
        vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());
        bool found = false;
        for (const auto& extension : extensions) {
            if (extension.extensionName == name) {
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::runtime_error("the requested instance extension is not available!");
        }
        renderer_data.instance_extensions.insert(name);
    }

    void renderer::add_device_extension(const std::string& name) {
        // cant do any checks without a physical device
        renderer_data.device_extensions.insert(name);
    }

    static void choose_extensions() {
        // we need the swapchain extension to present
        renderer::add_device_extension("VK_KHR_swapchain");

        // glfw provides the platform extensions required for creating a window surface
        uint32_t glfw_extension_count = 0;
        const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
        if (glfw_extension_count > 0) {
            for (uint32_t i = 0; i < glfw_extension_count; i++) {
                renderer::add_instance_extension(glfw_extensions[i]);
            }
        }

#ifndef NDEBUG
        // we don't want to be completely oblivious to what we're doing wrong
        renderer::add_layer("VK_LAYER_KHRONOS_validation");
        renderer::add_instance_extension("VK_EXT_debug_utils");
#endif

        // if we're not using vulkan 1.1, we need VK_KHR_maintenance1 to use viewports with negative
        // heights
        if (renderer_data.vulkan_version < VK_API_VERSION_1_1) {
            renderer::add_device_extension("VK_KHR_maintenance1");
        }

        // VK_KHR_portability_subset requires VK_KHR_get_physical_device_properties2
        renderer::add_instance_extension("VK_KHR_get_physical_device_properties2");
    }

    static void create_instance() {
        VkApplicationInfo app_info;
        util::zero(app_info);
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.apiVersion = renderer_data.vulkan_version;
        app_info.pApplicationName = app_info.pEngineName = "vkrollercoaster";
        app_info.applicationVersion = app_info.engineVersion =
            VK_MAKE_VERSION(1, 0, 0); // todo: git tags
        VkInstanceCreateInfo create_info;
        util::zero(create_info);
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;
        std::vector<const char*> extensions, layer_names;
        for (const auto& extension : renderer_data.instance_extensions) {
            extensions.push_back(extension.c_str());
        }
        for (const auto& layer_name : renderer_data.layer_names) {
            layer_names.push_back(layer_name.c_str());
        }
        if (!extensions.empty()) {
            create_info.enabledExtensionCount = extensions.size();
            create_info.ppEnabledExtensionNames = extensions.data();
        }
        if (!layer_names.empty()) {
            create_info.enabledLayerCount = layer_names.size();
            create_info.ppEnabledLayerNames = layer_names.data();
        }
        if (vkCreateInstance(&create_info, nullptr, &renderer_data.instance) != VK_SUCCESS) {
            throw std::runtime_error("could not create a vulkan instance!");
        }
    }

    static void create_debug_messenger() {
        auto fpCreateDebugUtilsMessengerEXT =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                renderer_data.instance, "vkCreateDebugUtilsMessengerEXT");
        if (fpCreateDebugUtilsMessengerEXT == nullptr) {
            // the extension was not enabled, so we cannot create a debug messenger
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT create_info;
        util::zero(create_info);
        create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        create_info.pfnUserCallback = validation_layer_callback;

        if (fpCreateDebugUtilsMessengerEXT(renderer_data.instance, &create_info, nullptr,
                                           &renderer_data.debug_messenger) != VK_SUCCESS) {
            throw std::runtime_error("could not create debug messenger");
        }
    }

    static bool check_device_extension_support(VkPhysicalDevice device) {
        uint32_t extension_count = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, extensions.data());
        std::set<std::string> remaining_extensions(renderer_data.device_extensions);
        for (const auto& extension : extensions) {
            remaining_extensions.erase(extension.extensionName);
        }
        return remaining_extensions.empty();
    }

    static bool is_device_suitable(VkPhysicalDevice device) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);

        std::set<bool> requirements_met = {
            renderer::find_queue_families(device).complete(),
            check_device_extension_support(device),
            properties.apiVersion >= renderer_data.vulkan_version,
        };
        return (requirements_met.size() == 1) && *requirements_met.begin();
    }

    static void pick_physical_device() {
        uint32_t device_count = 0;
        vkEnumeratePhysicalDevices(renderer_data.instance, &device_count, nullptr);
        if (device_count == 0) {
            throw std::runtime_error("no GPUs are installed on this system with Vulkan support!");
        }
        std::vector<VkPhysicalDevice> physical_devices(device_count);
        vkEnumeratePhysicalDevices(renderer_data.instance, &device_count, physical_devices.data());
        for (auto device : physical_devices) {
            if (is_device_suitable(device)) {
                VkPhysicalDeviceProperties properties;
                vkGetPhysicalDeviceProperties(device, &properties);
                spdlog::info("chose physical device: {0}", properties.deviceName);
                renderer_data.physical_device = device;
                return;
            }
        }
        throw std::runtime_error("no suitable GPU was found!");
    }

    static void create_logical_device() {
        auto indices = renderer::find_queue_families(renderer_data.physical_device);
        std::vector<VkDeviceQueueCreateInfo> queue_create_info;
        float queue_priority = 1.f;

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(renderer_data.physical_device, &queue_family_count,
                                                 nullptr);
        for (uint32_t i = 0; i < queue_family_count; i++) {
            VkDeviceQueueCreateInfo create_info;
            util::zero(create_info);
            create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            create_info.queueFamilyIndex = i;
            create_info.queueCount = 1;
            create_info.pQueuePriorities = &queue_priority;
            queue_create_info.push_back(create_info);
        }

        VkDeviceCreateInfo create_info;
        util::zero(create_info);
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = queue_create_info.size();
        create_info.pQueueCreateInfos = queue_create_info.data();

        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(renderer_data.physical_device, &features);
        create_info.pEnabledFeatures = &features;

        std::vector<const char*> layer_names, extensions;
        for (const auto& layer_name : renderer_data.layer_names) {
            layer_names.push_back(layer_name.c_str());
        }
        for (const auto& extension : renderer_data.device_extensions) {
            extensions.push_back(extension.c_str());
        }

        uint32_t supported_extension_count = 0;
        vkEnumerateDeviceExtensionProperties(renderer_data.physical_device, nullptr,
                                             &supported_extension_count, nullptr);
        std::vector<VkExtensionProperties> supported_extensions(supported_extension_count);
        vkEnumerateDeviceExtensionProperties(renderer_data.physical_device, nullptr,
                                             &supported_extension_count,
                                             supported_extensions.data());
        for (const auto& extension : supported_extensions) {
            bool found = false;
            for (auto enabled : extensions) {
                if (strcmp(enabled, extension.extensionName) == 0) {
                    found = true;
                    break;
                }
            }

            // if VK_KHR_portability_subset is present, it needs to be enabled
            static const char* const portability_subset_ext = "VK_KHR_portability_subset";
            if (!found && strcmp(extension.extensionName, portability_subset_ext) == 0) {
                extensions.push_back(portability_subset_ext);
            }
        }

        if (!layer_names.empty()) {
            create_info.enabledLayerCount = layer_names.size();
            create_info.ppEnabledLayerNames = layer_names.data();
        }

        if (!extensions.empty()) {
            create_info.enabledExtensionCount = extensions.size();
            create_info.ppEnabledExtensionNames = extensions.data();
        }

        if (vkCreateDevice(renderer_data.physical_device, &create_info, nullptr,
                           &renderer_data.device) != VK_SUCCESS) {
            throw std::runtime_error("could not create a logical device!");
        }

        vkGetDeviceQueue(renderer_data.device, *indices.graphics_family, 0,
                         &renderer_data.graphics_queue);
        vkGetDeviceQueue(renderer_data.device, *indices.compute_family, 0,
                         &renderer_data.compute_queue);
    }

    static void create_descriptor_pool() {
        std::vector<VkDescriptorPoolSize> pool_sizes = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };
        VkDescriptorPoolCreateInfo create_info;
        util::zero(create_info);
        create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        create_info.poolSizeCount = pool_sizes.size();
        create_info.pPoolSizes = pool_sizes.data();
        create_info.maxSets = 1000 * pool_sizes.size();
        create_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        if (vkCreateDescriptorPool(renderer_data.device, &create_info, nullptr,
                                   &renderer_data.descriptor_pool) != VK_SUCCESS) {
            throw std::runtime_error("could not create descriptor pool!");
        }
    }

    static void create_graphics_command_pool() {
        auto indices = renderer::find_queue_families(renderer_data.physical_device);
        VkCommandPoolCreateInfo create_info;
        util::zero(create_info);
        create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        create_info.queueFamilyIndex = *indices.graphics_family;
        create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(renderer_data.device, &create_info, nullptr,
                                &renderer_data.graphics_command_pool) != VK_SUCCESS) {
            throw std::runtime_error("could not create command pool!");
        }
    }

    static void create_sync_objects() {
        VkSemaphoreCreateInfo semaphore_create_info;
        util::zero(semaphore_create_info);
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fence_create_info;
        util::zero(fence_create_info);
        fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (auto& frame_data : renderer_data.frame_sync_objects) {
            if (vkCreateSemaphore(renderer_data.device, &semaphore_create_info, nullptr,
                                  &frame_data.image_available_semaphore) != VK_SUCCESS ||
                vkCreateSemaphore(renderer_data.device, &semaphore_create_info, nullptr,
                                  &frame_data.render_finished_semaphore) != VK_SUCCESS ||
                vkCreateFence(renderer_data.device, &fence_create_info, nullptr,
                              &frame_data.fence) != VK_SUCCESS) {
                throw std::runtime_error("could not create sync objects!");
            }
        }
    }

    struct camera_buffer_data {
        glm::mat4 projection = glm::mat4(1.f);
        glm::mat4 view = glm::mat4(1.f);
        glm::vec3 position = glm::vec3(0.f);
    };

    void renderer::init(uint32_t vulkan_version) {
        uint32_t major, minor, patch;
        expand_vulkan_version(vulkan_version, major, minor, patch);
        spdlog::info("initializing renderer... (with vulkan version {0}.{1}.{2})", major, minor,
                     patch);

        // initialize vulkan
        renderer_data.vulkan_version = vulkan_version;
        choose_extensions();
        create_instance();
        create_debug_messenger();
        pick_physical_device();
        create_logical_device();
        create_descriptor_pool();
        create_graphics_command_pool();
        create_sync_objects();
        allocator::init();

        // create white texture
        image_data white_data;
        int32_t channels = 4;
        white_data.data.resize(channels, 255);
        white_data.channels = channels;
        white_data.width = white_data.height = 1;
        renderer_data.white_texture = ref<texture>::create(ref<image2d>::create(white_data));

        // create global camera buffer
        renderer_data.camera_buffer = ref<uniform_buffer>::create(0, 0, sizeof(camera_buffer_data));
    }

    static void shutdown_renderer() {
        spdlog::info("shutting down renderer...");
        vkDeviceWaitIdle(renderer_data.device);
        for (const auto& frame_data : renderer_data.frame_sync_objects) {
            vkDestroyFence(renderer_data.device, frame_data.fence, nullptr);
            vkDestroySemaphore(renderer_data.device, frame_data.render_finished_semaphore, nullptr);
            vkDestroySemaphore(renderer_data.device, frame_data.image_available_semaphore, nullptr);
        }
        vkDestroyCommandPool(renderer_data.device, renderer_data.graphics_command_pool, nullptr);
        vkDestroyDescriptorPool(renderer_data.device, renderer_data.descriptor_pool, nullptr);
        vkDestroyDevice(renderer_data.device, nullptr);
        if (renderer_data.debug_messenger != nullptr) {
            auto fpDestroyDebugUtilsMessengerEXT =
                (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                    renderer_data.instance, "vkDestroyDebugUtilsMessengerEXT");
            if (fpDestroyDebugUtilsMessengerEXT != nullptr) {
                fpDestroyDebugUtilsMessengerEXT(renderer_data.instance,
                                                renderer_data.debug_messenger, nullptr);
            } else {
                spdlog::warn("created debug messenger but could not destroy it - will result in "
                             "memory leak");
            }
        }
        vkDestroyInstance(renderer_data.instance, nullptr);
    }

    void renderer::shutdown() {
        renderer_data._skybox.reset();
        renderer_data.camera_buffer.reset();
        renderer_data.white_texture.reset();
        if (renderer_data.track_model) {
            renderer_data.track_model.reset();
        }

        allocator::shutdown();
        vkDeviceWaitIdle(renderer_data.device);

        renderer_data.should_shutdown = true;
        if (renderer_data.ref_count == 0) {
            shutdown_renderer();
        }
    }

    void renderer::new_frame() {
        renderer_data.current_frame = (renderer_data.current_frame + 1) % max_frame_count;
    }

    void renderer::add_ref() { renderer_data.ref_count++; }
    void renderer::remove_ref() {
        renderer_data.ref_count--;
        if (renderer_data.ref_count == 0 && renderer_data.should_shutdown) {
            shutdown_renderer();
        }
    }

    static void render_model(ref<command_buffer> cmdbuffer, const transform_component& transform,
                             ref<model> _model, internal_cmdbuffer_data* internal_data) {
        auto target = cmdbuffer->get_current_render_target();
        if (!target) {
            throw std::runtime_error("cannot render outside of a render pass!");
        }
        // calculate transformation matrices
        struct {
            glm::mat4 model, normal;
        } push_constant_data;
        push_constant_data.normal = glm::toMat4(glm::quat(transform.rotation));
        push_constant_data.model = glm::translate(glm::mat4(1.f), transform.translation) *
                                   push_constant_data.normal *
                                   glm::scale(glm::mat4(1.f), transform.scale);

        const auto& buffer_data = _model->get_buffers();
        const auto& materials = _model->get_materials();
        for (const auto& [material_index, ibo] : buffer_data.indices) {
            // create pipeline
            ref<pipeline> _pipeline;
            {
                pipeline_spec spec;

                spec.input_layout = _model->get_input_layout();
                spec.enable_blending = true;
                spec.enable_depth_testing = true;

                ref<material> _material = materials[material_index];
                _pipeline = _material->create_pipeline(target, spec);
            }

            // set scissor
            VkRect2D scissor = _pipeline->get_scissor();
            vkCmdSetScissor(cmdbuffer->get(), 0, 1, &scissor);

            // set viewport
            VkViewport viewport = _pipeline->get_viewport();
            viewport.y = (float)target->get_extent().height - viewport.y;
            viewport.height *= -1.f;
            vkCmdSetViewport(cmdbuffer->get(), 0, 1, &viewport);

            // bind pipeline
            _pipeline->bind(cmdbuffer);

            // set vertex data
            buffer_data.vertices->bind(cmdbuffer);
            ibo->bind(cmdbuffer);

            // push constants
            vkCmdPushConstants(cmdbuffer->get(), _pipeline->get_layout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constant_data),
                               &push_constant_data);

            // render
            vkCmdDrawIndexed(cmdbuffer->get(), ibo->get_index_count(), 1, 0, 0, 0);

            submitted_render_call submitted_call;
            submitted_call._pipeline = _pipeline;
            submitted_call.vbo = buffer_data.vertices;
            submitted_call.ibo = ibo;
            submitted_call._skybox = renderer_data._skybox;
            internal_data->submitted_calls.push_back(submitted_call);
        }
    }

    void renderer::render_entity(ref<command_buffer> cmdbuffer, entity to_render) {
        if (!to_render.has_component<transform_component>() ||
            !to_render.has_component<model_component>()) {
            throw std::runtime_error(
                "the given entity does not have necessary components for rendering!");
        }

        ref<model> _model = to_render.get_component<model_component>().data;
        const auto& transform = to_render.get_component<transform_component>();

        render_model(cmdbuffer, transform, _model, cmdbuffer->m_internal_data);
    }

    void renderer::render_track(ref<command_buffer> cmdbuffer, entity track) {
        if (!renderer_data.track_model) {
            auto source = ref<model_source>::create("assets/models/track.gltf");
            renderer_data.track_model = ref<model>::create(source);
        }

        entity current_track = track;
        std::unordered_set<entity> rendererd_entities;
        while (rendererd_entities.find(current_track) == rendererd_entities.end()) {
            if (!current_track.has_component<transform_component>() ||
                !current_track.has_component<track_segment_component>()) {
                throw std::runtime_error("this track node does not have the necessary components!");
            }
            const auto& entity_transform = current_track.get_component<transform_component>();
            const auto& track_data = current_track.get_component<track_segment_component>();

            transform_component transform;
            transform.translation = entity_transform.translation;
            if (track_data.next) {
                glm::vec3 next_translation =
                    track_data.next.get_component<transform_component>().translation;
                glm::vec3 direction = glm::normalize(next_translation - transform.translation);

                glm::vec3 rotation = glm::vec3(0.f);

                rotation.x = -glm::asin(direction.y);
                float adjacent = glm::cos(rotation.x);
                rotation.y = glm::atan(direction.x / adjacent, direction.z / adjacent);

                transform.rotation = rotation;
            } else {
                transform.rotation = entity_transform.rotation;
            }
            transform.scale = entity_transform.scale;

            // todo: build model or something

            render_model(cmdbuffer, transform, renderer_data.track_model,
                         cmdbuffer->m_internal_data);
            rendererd_entities.insert(current_track);

            current_track = track_data.next;
            if (!current_track) {
                break;
            }
        }
    }

    ref<command_buffer> renderer::create_render_command_buffer() {
        auto instance = new command_buffer(renderer_data.graphics_command_pool,
                                           renderer_data.graphics_queue, false, true);
        return ref<command_buffer>(instance);
    }

    ref<command_buffer> renderer::create_single_time_command_buffer() {
        auto instance = new command_buffer(renderer_data.graphics_command_pool,
                                           renderer_data.graphics_queue, true, false);
        return ref<command_buffer>(instance);
    }

    uint32_t renderer::get_vulkan_version() { return renderer_data.vulkan_version; }
    VkInstance renderer::get_instance() { return renderer_data.instance; }
    VkPhysicalDevice renderer::get_physical_device() { return renderer_data.physical_device; }
    VkDevice renderer::get_device() { return renderer_data.device; }
    VkQueue renderer::get_graphics_queue() { return renderer_data.graphics_queue; }
    VkQueue renderer::get_compute_queue() { return renderer_data.compute_queue; }
    VkDescriptorPool renderer::get_descriptor_pool() { return renderer_data.descriptor_pool; }
    ref<texture> renderer::get_white_texture() { return renderer_data.white_texture; }

    ref<uniform_buffer> renderer::get_camera_buffer() { return renderer_data.camera_buffer; }
    void renderer::update_camera_buffer(ref<scene> _scene, ref<window> _window) {
        camera_buffer_data data;
        entity main_camera = _scene->find_main_camera();
        if (main_camera) {
            float aspect_ratio = _window->get_aspect_ratio();
            calculate_camera_matrices(main_camera, aspect_ratio, data.projection, data.view);

            const auto& transform = main_camera.get_component<transform_component>();
            data.position = transform.translation;
        }
        renderer_data.camera_buffer->set_data(data);
    }

    void renderer::calculate_camera_matrices(entity camera, float aspect_ratio,
                                             glm::mat4& projection, glm::mat4& view) {
        const auto& camera_data = camera.get_component<camera_component>();
        const auto& transform = camera.get_component<transform_component>();
        projection = glm::perspective(glm::radians(camera_data.fov), aspect_ratio, 0.1f, 256.f);
        glm::vec3 direction =
            glm::toMat4(glm::quat(transform.rotation)) * glm::vec4(0.f, 0.f, 1.f, 1.f);
        view = glm::lookAt(transform.translation, transform.translation + glm::normalize(direction),
                           camera_data.up);
    }

    ref<skybox> renderer::get_skybox() { return renderer_data._skybox; }
    bool renderer::load_skybox(const fs::path& path) {
        if (!fs::exists(path)) {
            return false;
        }

        auto img = ref<image_cube>::create(path);
        renderer_data._skybox = ref<skybox>::create(img);

        return true;
    }

    void renderer::expand_vulkan_version(uint32_t version, uint32_t& major, uint32_t& minor,
                                         uint32_t& patch) {
        // bit twiddling bullshit
        constexpr uint32_t major_offset = 22;
        constexpr uint32_t minor_offset = 12;
        major = version >> major_offset;
        minor = (version >> minor_offset) & util::create_mask(major_offset - minor_offset);
        patch = version & util::create_mask(minor_offset);
    }

    queue_family_indices renderer::find_queue_families(VkPhysicalDevice device) {
        queue_family_indices indices;
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count,
                                                 queue_families.data());
        for (uint32_t i = 0; i < queue_families.size(); i++) {
            const auto& queue_family = queue_families[i];
            if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphics_family = i;
            }
            if (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT) {
                indices.compute_family = i;
            }
            if (indices.complete()) {
                break;
            }
        }
        return indices;
    }

    const sync_objects& renderer::get_sync_objects(size_t frame_index) {
        return renderer_data.frame_sync_objects[frame_index];
    }

    size_t renderer::get_current_frame() { return renderer_data.current_frame; }
}; // namespace vkrollercoaster
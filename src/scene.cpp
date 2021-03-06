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
#include "scene.h"
#include "components.h"
namespace vkrollercoaster {
    void entity::add_to_entity_set() { this->m_scene->m_entities.insert(this); }
    void entity::remove_from_entity_set() { this->m_scene->m_entities.erase(this); }
    void scene::reset() {
        {
            auto copy = this->m_entities;
            for (entity* ent : copy) {
                ent->reset();
            }
        }
    }
    void scene::update() {
        // light data
        std::unordered_map<ref<light>, std::vector<entity>> lights;
        for (entity ent : this->view<transform_component, light_component>()) {
            ref<light> _light = ent.get_component<light_component>().data;
            lights[_light].push_back(ent);
        }
        for (const auto& [_light, entities] : lights) {
            _light->update_buffers(entities);
        }

        // scripts
        for (entity ent : this->view<script_component>()) {
            const auto& scripts = ent.get_component<script_component>();
            for (ref<script> _script : scripts.scripts) {
                if (!_script->enabled()) {
                    continue;
                }
                _script->update();
            }
        }
    }
    void scene::for_each(std::function<void(entity)> callback) {
        for (size_t i = 0; i < this->m_registry.size(); i++) {
            entity ent = entity((entt::entity)i, this);
            callback(ent);
        }
    }
    entity scene::create(const std::string& tag) {
        entt::entity id = this->m_registry.create();
        entity ent(id, this);

        // add basic components
        ent.add_component<transform_component>();
        ent.add_component<tag_component>().tag = tag;

        return ent;
    }
    void scene::reevaluate_first_track_node() {
        entity first_track_node;

        auto view = this->view<track_segment_component>();
        if (!view.empty()) {
            entity previous = view[0];

            do {
                first_track_node = previous;

                previous = entity();
                for (entity node : view) {
                    const auto& track_data = node.get_component<track_segment_component>();
                    if (track_data.next == first_track_node) {
                        previous = node;
                        break;
                    }
                }
            } while (previous);
        }

        this->m_first_track_node = first_track_node;
    }
    std::vector<entity> scene::find_tag(const std::string& tag) {
        std::vector<entity> entities;
        std::vector<entity> tag_view = this->view<tag_component>();
        for (entity ent : tag_view) {
            const auto& entity_tag = ent.get_component<tag_component>();
            if (entity_tag.tag == tag) {
                entities.push_back(ent);
            }
        }
        return entities;
    }
    entity scene::find_main_camera() {
        const auto& cameras = this->view<camera_component>();
        entity main_camera;
        if (!cameras.empty()) {
            for (entity camera : cameras) {
                if (camera.get_component<camera_component>().primary) {
                    main_camera = camera;
                    break;
                }
            }
            if (!main_camera) {
                main_camera = cameras[0];
            }
        }
        return main_camera;
    }
} // namespace vkrollercoaster
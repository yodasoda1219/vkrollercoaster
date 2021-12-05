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
#include "material.h"
#include "buffers.h"
#include <assimp/Importer.hpp>
struct aiScene;
struct aiNode;
struct aiMesh;
namespace vkrollercoaster {
    class model : public ref_counted {
    public:
        struct render_call_data {
            ref<material> _material;
            ref<vertex_buffer> vbo;
            ref<index_buffer> ibo;
        };
        struct vertex {
            glm::vec3 position, normal;
            glm::vec2 uv;
        };
        struct mesh {
            size_t vertex_offset, vertex_count, index_offset, index_count, material_index;
            aiNode* node;
            aiMesh* assimp_mesh;
        };
        model(const fs::path& path);
        model(const model&) = delete;
        model& operator=(const model&) = delete;
        void reload();

        const std::vector<vertex>& get_vertices() { return this->m_vertices; }
        const std::vector<uint32_t>& get_indices() { return this->m_indices; }
        const std::vector<mesh>& get_meshes() { return this->m_meshes; }
        const std::vector<render_call_data>& get_render_call_data() { return this->m_render_call_data; }
        const fs::path& get_path() { return this->m_path; }
        const vertex_input_data& get_input_layout() { return this->m_input_layout; }
    private:
        using material_map_t = std::map<size_t, std::vector<size_t>>;
        void process_node(aiNode* node, material_map_t& material_map);
        void process_mesh(aiMesh* mesh_, aiNode* node, material_map_t& material_map);
        void process_materials(std::vector<ref<material>>& materials);
        void create_render_call_data(const material_map_t& material_map, const std::vector<ref<material>>& materials);
        fs::path get_resource_path(const aiString& ai_path);
        std::vector<vertex> m_vertices;
        std::vector<uint32_t> m_indices;
        std::vector<mesh> m_meshes;
        std::vector<ref<material>> m_materials;
        std::vector<render_call_data> m_render_call_data;
        fs::path m_path;
        const aiScene* m_scene;
        std::unique_ptr<Assimp::Importer> m_importer;
        vertex_input_data m_input_layout;
    };
}
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

struct ps_input {
    [[vk::location(0)]] float3 normal : NORMAL0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 tangent : TANGENT0;

    [[vk::location(3)]] float3 fragment_position : NORMAL1;
    [[vk::location(4)]] float3 camera_position : NORMAL2;
};

// light data
struct attenuation_settings {
    float _constant, _linear, _quadratic;
};
struct directional_light {
    float3 diffuse_color, specular_color, ambient_color;
    float3 direction;
    float ambient_strength, specular_strength;
};
struct point_light {
    float3 diffuse_color, specular_color, ambient_color;
    float3 position;
    float ambient_strength, specular_strength;
    attenuation_settings attenuation;
};
struct spotlight {
    float3 diffuse_color, specular_color, ambient_color;
    float3 position, direction;
    float ambient_strength, specular_strength;
    float cutoff, outer_cutoff;
    attenuation_settings attenuation;
};
struct light_data_t {
    int spotlight_count, point_light_count, directional_light_count;
    directional_light directional_lights[30];
    point_light point_lights[30];
    spotlight spotlights[30];
};
[[vk::binding(0, 1)]] ConstantBuffer<light_data_t> light_data;

// material data
struct material_data_t {
    bool use_normal_map;
    float shininess, opacity;
    float3 albedo_color, specular_color;
};
[[vk::binding(1, 1)]] ConstantBuffer<material_data_t> material_data;

// albedo texture
[[vk::binding(2, 1)]] Texture2D albedo_texture;
[[vk::binding(2, 1)]] SamplerState albedo_sampler;

// specular texture
[[vk::binding(3, 1)]] Texture2D specular_texture;
[[vk::binding(3, 1)]] SamplerState specular_sampler;

// normal map
[[vk::binding(4, 1)]] Texture2D normal_map;
[[vk::binding(4, 1)]] SamplerState normal_sampler;

struct color_data_t {
    float3 albedo, specular;
};
color_data_t get_color_data(float2 uv) {
    color_data_t data;

    data.albedo = albedo_texture.Sample(albedo_sampler, uv).rgb * material_data.albedo_color;
    data.specular = specular_texture.Sample(specular_sampler, uv).rgb * material_data.specular_color;

    return data;
}

//=== obsolete lighting ====
float calculate_attenuation(attenuation_settings attenuation, float3 light_position, ps_input stage_input) {
    float distance_ = length(light_position - stage_input.fragment_position);
    float distance_2 = distance_ * distance_;
    return 1.f / (attenuation._constant + attenuation._linear * distance_ + attenuation._quadratic * distance_2);
}

float calculate_specular(float3 light_direction, ps_input stage_input, float3 normal) {
    float3 view_direction = normalize(stage_input.camera_position - stage_input.fragment_position);
    float3 reflect_direction = reflect(-light_direction, normal);
    return pow(max(dot(view_direction, reflect_direction), 0.f), material_data.shininess);
}
float calculate_diffuse(float3 light_direction, float3 normal) {
    return max(dot(normal, light_direction), 0.f);
}

float3 calculate_spotlight(int index, color_data_t color_data, ps_input stage_input, float3 normal) {
    spotlight light = light_data.spotlights[index];
    float3 light_direction = normalize(light.position - stage_input.fragment_position);

    float3 ambient = light.ambient_color;
    float3 specular = calculate_specular(light_direction, stage_input, normal) * light.specular_color;
    float3 diffuse = calculate_diffuse(light_direction, normal) * light.diffuse_color;

    float3 color = ((ambient + diffuse) * color_data.albedo) + (specular * color_data.specular);
    float attenuation = calculate_attenuation(light.attenuation, light.position, stage_input);

    float theta = dot(light_direction, normalize(-light.direction));
    float epsilon = light.cutoff - light.outer_cutoff;
    float intensity = clamp((theta - light.outer_cutoff) / epsilon, 0.f, 1.f);
    
    return color * attenuation * intensity;
}
float3 calculate_point_light(int index, color_data_t color_data, ps_input stage_input, float3 normal) {
    point_light light = light_data.point_lights[index];
    float3 light_direction = normalize(light.position - stage_input.fragment_position);

    float3 ambient = light.ambient_color;
    float3 specular = calculate_specular(light_direction, stage_input, normal) * light.specular_color;
    float3 diffuse = calculate_diffuse(light_direction, normal) * light.diffuse_color;

    float3 color = ((ambient + diffuse) * color_data.albedo) + (specular * color_data.specular);
    float attenuation = calculate_attenuation(light.attenuation, light.position, stage_input);

    return color * attenuation;
}
float3 calculate_directional_light(int index, color_data_t color_data, ps_input stage_input, float3 normal) {
    directional_light light = light_data.directional_lights[index];
    // todo: calculate directional light
    return float3(0.f);
}

float3 calculate_normal(ps_input input) {
    if (!material_data.use_normal_map) {
        return input.normal;
    }

    float3 tangent_normal = normal_map.Sample(normal_sampler, input.uv).xyz * 2.f - 1.f;

    float3 n = input.normal; // already normalized
    float3 t = input.tangent; // also already normalized
    float3 b = normalize(cross(n, t));
    float3x3 tbn = transpose(float3x3(t, b, n));

    return normalize(mul(tbn, tangent_normal));
}

float4 main(ps_input input) : SV_TARGET {
    color_data_t color_data = get_color_data(input.uv);
    float3 normal = calculate_normal(input);
    float3 fragment_color = float3(0.f);
    for (int i = 0; i < light_data.spotlight_count; i++) {
        fragment_color += calculate_spotlight(i, color_data, input, normal);
    }
    for (int i = 0; i < light_data.point_light_count; i++) {
        fragment_color += calculate_point_light(i, color_data, input, normal);
    }
    for (int i = 0; i < light_data.directional_light_count; i++) {
        fragment_color += calculate_directional_light(i, color_data, input, normal);
    }
    return float4(fragment_color, material_data.opacity);
}
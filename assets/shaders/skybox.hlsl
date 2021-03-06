#stage vertex
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

struct vs_output {
    float4 position : SV_POSITION;
    [[vk::location(0)]] float3 uvw : TEXCOORD0;
};

struct camera_data {
    float4x4 projection;
    float4x4 view;
};
[[vk::binding(0, 0)]] ConstantBuffer<camera_data> camera;

vs_output main(float3 position : POSITION0) {
    vs_output output;

    float4x4 model = float4x4(float3x3(camera.view));
    output.position = mul(camera.projection, mul(model, float4(position, 1.f)));
    output.uvw = position * float3(1.f, -1.f, 1.f);

    return output;
}
#stage pixel
[[vk::binding(1, 0)]] TextureCube environment_texture;
[[vk::binding(1, 0)]] SamplerState environment_sampler;

struct skybox_data_t {
    float exposure, gamma;
};
[[vk::binding(2, 0)]] ConstantBuffer<skybox_data_t> skybox_data;

// http://filmicworlds.com/blog/filmic-tonemapping-operators/
const float a = 0.15f;
const float b = 0.5f;
const float c = 0.1f;
const float d = 0.2f;
const float e = 0.02f;
const float f = 0.3f;
const float w = 11.2f;
float3 uncharted_to_tonemap(float3 x) {
    return ((x*(a*x+c*b)+d*e)/(x*(a*x+b)+d*f))-e/f;
}

float4 main([[vk::location(0)]] float3 uvw : TEXCOORD0) : SV_TARGET {
    float3 color = environment_texture.Sample(environment_sampler, uvw).rgb;

	// tone mapping
	color = uncharted_to_tonemap(color * skybox_data.exposure);
	color /= uncharted_to_tonemap(w.xxx);

	// gamma correction
	color = pow(color, (1.f / skybox_data.gamma).xxx);

	return float4(color, 1.f);
}
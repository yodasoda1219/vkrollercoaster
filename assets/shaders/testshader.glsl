#stage vertex
#version 460
layout(location = 0) in vec3 position;
void main() {
    gl_Position = vec4(position, 1.0);
}
#stage fragment
#version 460
layout(location = 0) out vec4 color;
void main() {
    color = vec4(0.0, 1.0, 0.0, 1.0);
}
#version 450
layout(location = 0) in vec3 color;
layout(location = 0) out vec4 o;
layout(set = 0, binding = 0) uniform U { vec4 scale; } u;
layout(push_constant) uniform P { vec4 scale; } p;
void main() { o = vec4(color, 1.0) * u.scale * p.scale; }

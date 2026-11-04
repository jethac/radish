#version 450
layout(location = 0) in highp vec4 pos;
layout(location = 1) in vec4 col;
layout(location = 0) out vec4 color;
void main() { gl_PointSize = 1.0; gl_Position = pos; color = col; }

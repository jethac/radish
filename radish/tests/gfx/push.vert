#version 450
layout(push_constant) uniform P { vec4 color; float z; } p;
void main()
{
   int i = gl_VertexIndex;
   float x = i == 0 ? -0.5 : (i == 1 ? 0.5 : 0.0);
   float y = i == 2 ? 0.5 : -0.5;
   gl_Position = vec4(x, y, p.z, 1.0);
}

#version 450
// Triangle (-0.5,-0.5), (0.5,-0.5), (0,0.5); red, green, blue at the corners.
// No arrays (a dynamically indexed constant array would need the constant UBO).
layout(location = 0) out vec3 color;
void main()
{
   int i = gl_VertexIndex;
   float x = i == 0 ? -0.5 : (i == 1 ? 0.5 : 0.0);
   float y = i == 2 ? 0.5 : -0.5;
   gl_Position = vec4(x, y, 0.0, 1.0);
   color = vec3(i == 0 ? 1.0 : 0.0, i == 1 ? 1.0 : 0.0, i == 2 ? 1.0 : 0.0);
}

#version 460

const vec3 positions[3] = vec3[](
    vec3(0.0, -0.5, 0.0), // top
    vec3(-0.5, 0.5, 0.0), // bottom left
    vec3(0.5, 0.5, 0.0) // bottom right
  );

const vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0), // red
    vec3(0.0, 1.0, 0.0), // green
    vec3(0.0, 0.0, 1.0) // blue
  );

layout(location = 0) out vec3 outColor;

void main(void) {
  gl_Position = vec4(positions[gl_VertexIndex], 1.0);
  outColor = colors[gl_VertexIndex];
}

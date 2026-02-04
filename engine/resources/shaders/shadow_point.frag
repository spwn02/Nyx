// Shadow depth fragment shader for point lights (cubemap)
// Converts linear depth to normalized distance for cubemap storage
#version 460 core

in VS_OUT {
  vec3 fragPos;
} fs_in;

uniform vec3 uLightPos;
uniform float uFarPlane;

void main() {
  // Compute linear distance from light to fragment
  float distance = length(fs_in.fragPos - uLightPos);
  
  // Normalize to [0, 1] range
  float normalizedDistance = distance / uFarPlane;
  
  // Store in depth map
  gl_FragDepth = normalizedDistance;
}

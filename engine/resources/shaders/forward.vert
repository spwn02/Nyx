#version 460

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;

uniform mat4 u_ViewProj;
uniform mat4 u_Model;
uniform mat4 u_View;

out vec3 vN;
out vec3 vP;
out vec3 vPV;

void main() {
  vec4 wp = u_Model * vec4(aPos, 1.0);
  vP = wp.xyz;
  vPV = (u_View * wp).xyz;

  mat3 nrmM = mat3(transpose(inverse(u_Model)));
  vN = normalize(nrmM * aNrm);

  gl_Position = u_ViewProj * wp;
}

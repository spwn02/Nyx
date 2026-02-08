#version 460 core

layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aTan; // xyz tangent, w sign (0 = missing)
layout(location=3) in vec2 aUV;

uniform mat4 u_ViewProj;
uniform mat4 u_Model;
uniform vec3 u_CamPos;

out VS_OUT {
  vec3 posW;
  vec3 nrmW;
  vec2 uv;
  vec2 uv0;
  vec4 tanW; // tangent in world + sign
  vec3 viewDirW;
} v;

void main() {
  vec4 wp = u_Model * vec4(aPos, 1.0);
  v.posW = wp.xyz;

  mat3 NrmM = mat3(transpose(inverse(u_Model)));
  v.nrmW = normalize(NrmM * aNrm);

  // For rigid transforms you could use mat3(u_Model), but keep correct version.
  vec3 tW = (abs(aTan.w) < 1e-6) ? vec3(0.0)
                                : normalize(mat3(u_Model) * aTan.xyz);
  v.tanW = vec4(tW, aTan.w);
  v.uv = aUV;
  v.uv0 = aUV;
  v.viewDirW = normalize(u_CamPos - v.posW);

  gl_Position = u_ViewProj * wp;
}

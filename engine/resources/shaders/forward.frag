#version 460

layout(location = 0) out vec4 oColor;
layout(location = 1) out uint oID;

uniform uint u_PickID;
uniform uint u_MaterialIndex;
uniform uint u_ViewMode;

in vec3 vN;
in vec3 vP;

struct GpuMaterialPacked {
  vec4 baseColor;
  vec4 mrAoCut; // x=metallic y=roughness z=ao w=alphaCutoff
  vec4 flags;   // reserved
};

layout(std430, binding = 14) readonly buffer MaterialsSSBO {
  GpuMaterialPacked mats[];
}
gMats;

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
  return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float D_GGX(float NoH, float a) {
  float a2 = a * a;
  float d = (NoH * NoH) * (a2 - 1.0) + 1.0;
  return a2 / (3.14159265 * d * d);
}

float V_SmithGGX(float NoV, float NoL, float a) {
  float a2 = a * a;
  float gv = NoL * sqrt(NoV * (NoV - NoV * a2) + a2);
  float gl = NoV * sqrt(NoL * (NoL - NoL * a2) + a2);
  return 0.5 / max(gv + gl, 1e-5);
}

vec3 BRDF(vec3 N, vec3 V, vec3 L, vec3 baseColor, float metallic,
          float roughness) {
  vec3 H = normalize(V + L);

  float NoV = saturate(dot(N, V));
  float NoL = saturate(dot(N, L));
  float NoH = saturate(dot(N, H));
  float VoH = saturate(dot(V, H));

  float a = max(0.04, roughness * roughness);

  vec3 F0 = mix(vec3(0.04), baseColor, metallic);
  vec3 F = fresnelSchlick(VoH, F0);

  float D = D_GGX(NoH, a);
  float Vg = V_SmithGGX(NoV, NoL, a);

  vec3 spec = (D * Vg) * F;
  vec3 kd = (1.0 - F) * (1.0 - metallic);
  vec3 diff = kd * baseColor * (1.0 / 3.14159265);

  return (diff + spec) * NoL;
}

void main() {
  if (u_ViewMode == 7u) {
    uint id = u_PickID;
    vec3 c = vec3(float((id) & 255u) / 255.0, float((id >> 8u) & 255u) / 255.0,
                  float((id >> 16u) & 255u) / 255.0);
    oColor = vec4(c, 1.0);
    oID = u_PickID;
    return;
  }

  GpuMaterialPacked m = gMats.mats[u_MaterialIndex];

  vec3 baseColor = m.baseColor.rgb;

  if (u_ViewMode == 1u) {
    oColor = vec4(baseColor, 1.0);
    oID = u_PickID;
    return;
  }

  float metallic = clamp(m.mrAoCut.x, 0.0, 1.0);
  float roughness = clamp(m.mrAoCut.y, 0.02, 1.0);

  if (u_ViewMode == 3u) {
    oColor = vec4(vec3(roughness), 1.0);
    oID = u_PickID;
    return;
  }

  if (u_ViewMode == 4u) {
    oColor = vec4(vec3(metallic), 1.0);
    oID = u_PickID;
    return;
  }

  float ao = clamp(m.mrAoCut.z, 0.0, 1.0);

  if (u_ViewMode == 5u) {
    oColor = vec4(vec3(ao), 1.0);
    oID = u_PickID;
    return;
  }

  vec3 N = normalize(vN);

  if (u_ViewMode == 2u) {
    oColor = vec4(N * 0.5 + 0.5, 1.0);
    oID = u_PickID;
    return;
  }

  // Camera vector stub: assume camera at origin (we'll replace with Frame UBO)
  vec3 V = normalize(-vP);

  // Fake sun (LightSystem later)
  vec3 L = normalize(vec3(0.3, 1.0, 0.2));
  vec3 sunColor = vec3(5.0);

  vec3 direct = BRDF(N, V, L, baseColor, metallic, roughness) * sunColor;
  vec3 ambient = baseColor * 0.05 * ao;

  vec3 hdr = direct + ambient;

  oColor = vec4(hdr, 1.0);
  oID = u_PickID;
}

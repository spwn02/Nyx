#version 460

layout(location = 0) out vec4 oColor;
layout(location = 1) out uint oID;

uniform uint u_PickID;
uniform uint u_MaterialIndex;
uniform uint u_ViewMode;
uniform int u_IsLight;
uniform vec4 u_LightColorIntensity; // rgb=color, a=intensity
uniform float u_LightExposure;
layout(binding = 6) uniform sampler2DArray uShadowDir;
layout(binding = 7) uniform sampler2DArray uShadowSpot;
layout(binding = 8) uniform samplerCubeArray uShadowPoint;
uniform vec4 u_CSM_Splits;
uniform vec2 u_ShadowDirSize;
uniform vec2 u_ShadowSpotSize;

in vec3 vN;
in vec3 vP;
in vec3 vPV;

struct GpuMaterialPacked {
  vec4 baseColor;
  vec4 mrAoCut; // x=metallic y=roughness z=ao w=alphaCutoff
  vec4 flags;   // reserved
};

layout(std430, binding = 14) readonly buffer MaterialsSSBO {
  GpuMaterialPacked mats[];
}
gMats;

struct GpuLight {
  vec4 posRadius;
  vec4 dirType;
  vec4 colorIntensity;
  vec4 spotParams;
  uvec4 shadow;
};

layout(std430, binding = 16) readonly buffer LightsSSBO {
  uvec4 header;
  GpuLight lights[];
}
gLights;

layout(std430, binding = 17) readonly buffer ShadowDirMatrices {
  mat4 dirVP[];
}
gShadowDir;

layout(std430, binding = 18) readonly buffer ShadowSpotMatrices {
  mat4 spotVP[];
}
gShadowSpot;

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

float sampleShadowArrayPCF(sampler2DArray sh, vec3 shadowNDC, int layer,
                           float bias, vec2 size) {
  vec2 uv = shadowNDC.xy;
  float z = shadowNDC.z;
  if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
    return 0.0;

  vec2 texel = 1.0 / size;
  float occl = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      vec2 o = vec2(x, y) * texel;
      float dz = texture(sh, vec3(uv + o, float(layer))).r;
      occl += (z - bias > dz) ? 1.0 : 0.0;
    }
  }
  occl /= 9.0;
  return occl;
}

int chooseCascade(float viewDepth) {
  if (viewDepth <= u_CSM_Splits.x)
    return 0;
  if (viewDepth <= u_CSM_Splits.y)
    return 1;
  if (viewDepth <= u_CSM_Splits.z)
    return 2;
  return 3;
}

vec3 toShadowNDC(mat4 lightVP, vec3 posW) {
  vec4 sh = lightVP * vec4(posW, 1.0);
  sh.xyz /= sh.w;
  return sh.xyz * 0.5 + 0.5;
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

  if (u_IsLight != 0) {
    float intensity = u_LightColorIntensity.a;
    if (u_LightExposure != 0.0) {
      intensity *= exp2(u_LightExposure);
    }
    vec3 lightRgb = u_LightColorIntensity.rgb * intensity;
    oColor = vec4(lightRgb, 1.0);
    oID = u_PickID;
    return;
  }

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

  vec3 V = normalize(-vPV);

  float viewDepth = -vPV.z;
  int csmIdx = chooseCascade(viewDepth);

  vec3 direct = vec3(0.0);
  uint lightCount = gLights.header.x;
  for (uint i = 0u; i < lightCount; ++i) {
    GpuLight Lg = gLights.lights[i];
    uint type = uint(Lg.dirType.w + 0.5);
    vec3 Ldir = vec3(0.0);
    float atten = 1.0;

    if (type == 2u) {
      Ldir = normalize(-Lg.dirType.xyz);
    } else {
      vec3 toL = Lg.posRadius.xyz - vP;
      float dist = max(length(toL), 1e-4);
      Ldir = toL / dist;
      float radius = max(Lg.posRadius.w, 0.001);
      float falloff = saturate(1.0 - dist / radius);
      atten = falloff * falloff;
    }

    if (type == 1u) {
      float cosInner = Lg.spotParams.x;
      float cosOuter = Lg.spotParams.y;
      float cosAng = dot(normalize(-Lg.dirType.xyz), Ldir);
      float spot = saturate((cosAng - cosOuter) / max(cosInner - cosOuter, 1e-4));
      atten *= spot;
    }

    vec3 lightCol = Lg.colorIntensity.rgb * Lg.colorIntensity.a;
    float shadow = 1.0;

    const uint shadowIndex = Lg.shadow.x;
    const bool hasShadow = (shadowIndex != 0xFFFFFFFFu);
    if (hasShadow) {
      float ndl = max(dot(N, Ldir), 0.0);
      float baseBias = 0.0006;
      float slopeBias = 0.0025 * (1.0 - ndl);
      float bias = baseBias + slopeBias;

      if (type == 2u) {
        int layer = int(shadowIndex) * 4 + csmIdx;
        vec3 shNDC = toShadowNDC(gShadowDir.dirVP[layer], vP);
        float occl = sampleShadowArrayPCF(uShadowDir, shNDC, layer, bias,
                                          u_ShadowDirSize);
        shadow = 1.0 - occl;
      } else if (type == 1u) {
        int layer = int(shadowIndex);
        vec3 shNDC = toShadowNDC(gShadowSpot.spotVP[layer], vP);
        float occl = sampleShadowArrayPCF(uShadowSpot, shNDC, layer, bias,
                                          u_ShadowSpotSize);
        shadow = 1.0 - occl;
      } else {
        vec3 toL = vP - Lg.posRadius.xyz;
        float dist = max(length(toL), 1e-4);
        vec3 dir = toL / dist;
        float farZ = max(Lg.posRadius.w, 0.001);
        float z = dist / farZ;
        float dz = texture(uShadowPoint, vec4(dir, float(shadowIndex))).r;
        shadow = (z - bias > dz) ? 0.0 : 1.0;
      }
    }
    direct += BRDF(N, V, Ldir, baseColor, metallic, roughness) * lightCol *
              atten * shadow;
  }

  vec3 ambient = baseColor * 0.05 * ao;

  vec3 hdr = direct + ambient;

  oColor = vec4(hdr, 1.0);
  oID = u_PickID;
}

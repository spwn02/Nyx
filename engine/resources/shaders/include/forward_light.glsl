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

vec3 computeDirectLightingForward(vec3 N, vec3 V, vec3 posW,
                                  float viewDepth, vec3 baseColor,
                                  float metallic, float roughness) {
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
      vec3 toL = Lg.posRadius.xyz - posW;
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
      float spot = saturate((cosAng - cosOuter) /
                            max(cosInner - cosOuter, 1e-4));
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
        vec3 shNDC = toShadowNDC(gShadowDir.dirVP[layer], posW);
        float occl = sampleShadowArrayPCF(uShadowDir, shNDC, layer, bias,
                                          u_ShadowDirSize);
        shadow = 1.0 - occl;
      } else if (type == 1u) {
        int layer = int(shadowIndex);
        vec3 shNDC = toShadowNDC(gShadowSpot.spotVP[layer], posW);
        float occl = sampleShadowArrayPCF(uShadowSpot, shNDC, layer, bias,
                                          u_ShadowSpotSize);
        shadow = 1.0 - occl;
      } else {
        vec3 toL = posW - Lg.posRadius.xyz;
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

  return direct;
}

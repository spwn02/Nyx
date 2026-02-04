int chooseCascade(float viewDepth) {
  int cc = int(max(uMisc.x, 1.0));
  if (cc <= 1)
    return 0;
  if (viewDepth < uSplitDepths.x)
    return 0;
  if (cc == 2)
    return 1;
  if (viewDepth < uSplitDepths.y)
    return 1;
  if (cc == 3)
    return 2;
  if (viewDepth < uSplitDepths.z)
    return 2;
  return min(3, cc - 1);
}

float sampleShadowCSM(vec3 posW, vec3 N, vec3 Ldir) {
  vec4 viewPos = u_View * vec4(posW, 1.0);
  float viewDepth = -viewPos.z;
  int c = chooseCascade(viewDepth);

  vec4 lp = uLightVP[c] * vec4(posW, 1.0);
  if (abs(lp.w) < 1e-6)
    return 1.0;

  vec3 lndc = lp.xyz / lp.w;
  vec2 luv = lndc.xy * 0.5 + 0.5;
  float ldepth01 = lndc.z * 0.5 + 0.5;
  if (luv.x < 0.0 || luv.x > 1.0 || luv.y < 0.0 || luv.y > 1.0)
    return 1.0;

  vec2 uvMin = uAtlasUVMin[c].xy;
  vec2 uvMax = uAtlasUVMax[c].xy;
  vec2 uv = mix(uvMin, uvMax, luv);

  float ndl = max(dot(N, Ldir), 0.0);
  float bias = max(uBiasParams.y, 0.0) + uBiasParams.z * (1.0 - ndl);
  float dcmp = ldepth01 - bias;

  vec2 invTile = (uvMax - uvMin) * uShadowMapSize.zw;
  float sum = 0.0;
  float wsum = 0.0;
  const int R = 1;
  for (int y = -R; y <= R; ++y) {
    for (int x = -R; x <= R; ++x) {
      vec2 off = vec2(float(x), float(y)) * invTile;
      float smp = texture(uShadowCSM, uv + off).r;
      float lit = (dcmp <= smp) ? 1.0 : 0.0;
      sum += lit;
      wsum += 1.0;
    }
  }
  return (wsum > 0.0) ? (sum / wsum) : 1.0;
}

uvec4 shadowMetaHeader() {
  return floatBitsToUint(gShadowMeta.meta[0]);
}

uint shadowMetaSpotCount() { return shadowMetaHeader().x; }
uint shadowMetaDirCount() { return shadowMetaHeader().y; }
uint shadowMetaPointCount() { return shadowMetaHeader().z; }

uint spotMetaBase() { return 1u; }
uint dirMetaBase() { return spotMetaBase() + shadowMetaSpotCount() * 6u; }
uint pointMetaBase() { return dirMetaBase() + shadowMetaDirCount() * 6u; }

mat4 readMat4(uint base) {
  return mat4(gShadowMeta.meta[base + 0u],
              gShadowMeta.meta[base + 1u],
              gShadowMeta.meta[base + 2u],
              gShadowMeta.meta[base + 3u]);
}

float sampleShadowAtlas(sampler2D atlas, mat4 lightVP, vec2 uvMin, vec2 uvMax,
                        vec3 posW, vec3 N, vec3 Ldir) {
  vec4 lp = lightVP * vec4(posW, 1.0);
  if (abs(lp.w) < 1e-6)
    return 1.0;
  vec3 lndc = lp.xyz / lp.w;
  vec2 luv = lndc.xy * 0.5 + 0.5;
  float ldepth01 = lndc.z * 0.5 + 0.5;
  if (luv.x < 0.0 || luv.x > 1.0 || luv.y < 0.0 || luv.y > 1.0)
    return 1.0;

  vec2 uv = mix(uvMin, uvMax, luv);

  float ndl = max(dot(N, Ldir), 0.0);
  float bias = max(uBiasParams.y, 0.0) + uBiasParams.z * (1.0 - ndl);
  float dcmp = ldepth01 - bias;

  vec2 invTile = (uvMax - uvMin) * uShadowMapSize.zw;
  float sum = 0.0;
  float wsum = 0.0;
  const int R = 1;
  for (int y = -R; y <= R; ++y) {
    for (int x = -R; x <= R; ++x) {
      vec2 off = vec2(float(x), float(y)) * invTile;
      float smp = texture(atlas, uv + off).r;
      float lit = (dcmp <= smp) ? 1.0 : 0.0;
      sum += lit;
      wsum += 1.0;
    }
  }
  return (wsum > 0.0) ? (sum / wsum) : 1.0;
}

float sampleShadowSpot(uint idx, vec3 posW, vec3 N, vec3 Ldir) {
  if (idx >= shadowMetaSpotCount())
    return 1.0;
  uint base = spotMetaBase() + idx * 6u;
  vec2 uvMin = gShadowMeta.meta[base + 0u].xy;
  vec2 uvMax = gShadowMeta.meta[base + 1u].xy;
  mat4 vp = readMat4(base + 2u);
  return sampleShadowAtlas(uShadowSpot, vp, uvMin, uvMax, posW, N, Ldir);
}

float sampleShadowDir(uint idx, vec3 posW, vec3 N, vec3 Ldir) {
  if (idx >= shadowMetaDirCount())
    return 1.0;
  uint base = dirMetaBase() + idx * 6u;
  vec2 uvMin = gShadowMeta.meta[base + 0u].xy;
  vec2 uvMax = gShadowMeta.meta[base + 1u].xy;
  mat4 vp = readMat4(base + 2u);
  return sampleShadowAtlas(uShadowDir, vp, uvMin, uvMax, posW, N, Ldir);
}

int cubeFaceFromDir(vec3 d) {
  vec3 ad = abs(d);
  if (ad.x >= ad.y && ad.x >= ad.z)
    return d.x > 0.0 ? 0 : 1;
  if (ad.y >= ad.x && ad.y >= ad.z)
    return d.y > 0.0 ? 2 : 3;
  return d.z > 0.0 ? 4 : 5;
}

vec2 cubeUVFromDir(vec3 d, int face) {
  vec3 ad = abs(d);
  vec2 uv;
  if (face == 0) { // +X
    uv = vec2(-d.z, -d.y) / ad.x;
  } else if (face == 1) { // -X
    uv = vec2(d.z, -d.y) / ad.x;
  } else if (face == 2) { // +Y
    uv = vec2(d.x, d.z) / ad.y;
  } else if (face == 3) { // -Y
    uv = vec2(d.x, -d.z) / ad.y;
  } else if (face == 4) { // +Z
    uv = vec2(d.x, -d.y) / ad.z;
  } else { // -Z
    uv = vec2(-d.x, -d.y) / ad.z;
  }
  return uv * 0.5 + 0.5;
}

float sampleShadowPoint(uint idx, vec3 posW, vec3 N) {
  if (idx >= shadowMetaPointCount())
    return 1.0;
  uint base = pointMetaBase() + idx * 2u;
  vec4 posAndFar = gShadowMeta.meta[base + 0u];
  vec4 arr = gShadowMeta.meta[base + 1u];
  uint arrayIndex = floatBitsToUint(arr.x);
  vec3 toL = posW - posAndFar.xyz;
  float dist = max(length(toL), 1e-4);
  vec3 dir = toL / dist;
  int face = cubeFaceFromDir(dir);
  vec2 uv = cubeUVFromDir(dir, face);
  int layer = int(arrayIndex * 6u + uint(face));
  float farZ = max(posAndFar.w, 0.001);
  float z = dist / farZ;
  float bias = 0.002;
  float dz = texture(uShadowPoint, vec3(uv, float(layer))).r;
  return (z - bias > dz) ? 0.0 : 1.0;
}

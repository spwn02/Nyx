const uint Alpha_Opaque = 0u;
const uint Alpha_Mask = 1u;
const uint Alpha_Blend = 2u;

#ifndef NONUNIFORM
#define NONUNIFORM(x) (x)
#endif

#ifndef MAT_REMAP_TEX
#define MAT_REMAP_TEX(x) (x)
#endif

#ifndef MAT_UV0
#define MAT_UV0 f.uv0
#endif

#ifndef NYX_MAX_TEX
#define NYX_MAX_TEX 256
#endif

vec3 srgbToLinear(vec3 c) { return pow(c, vec3(2.2)); }

vec4 regfile[128];

vec4 loadConst4(uint a, uint b, uint c, uint d) {
  return vec4(uintBitsToFloat(a), uintBitsToFloat(b), uintBitsToFloat(c),
              uintBitsToFloat(d));
}

vec3 decodeNormalTS(vec3 n) {
  n = n * 2.0 - 1.0;
  return normalize(n);
}

void evalMaterial(out vec3 baseColor, out float metallic, out float roughness,
                  out float ao, out vec3 emissive, out vec3 normalWS,
                  out float alpha, out uint alphaMode, out float alphaCutoff) {

  GpuMatGraphHeader h = H[u_MaterialIndex];
  alphaMode = h.alphaMode;
  alphaCutoff = h.alphaCutoff;

  // Builtins (compiler contract)
  regfile[0] = vec4(MAT_UV0, 0, 0);
  regfile[1] = vec4(normalize(f.nrmW), 0);
  regfile[2] = vec4(normalize(f.viewDirW), 0);

  // Build TBN
  vec3 T = normalize(f.tanW.xyz);
  vec3 Nw = normalize(f.nrmW);
  vec3 B = cross(Nw, T) * f.tanW.w;
  mat3 TBN = mat3(T, B, Nw);

  uint start = h.nodeOffset;
  uint end = start + h.nodeCount;

  for (uint i = start; i < end; ++i) {
    GpuMatNode n = N[i];
    uint op = n.op;

    if (op == 0u /*Const4*/) {
      regfile[n.dst] = loadConst4(n.a, n.b, n.c, n.extra);
    } else if (op == 3u /*Add*/) {
      regfile[n.dst] = regfile[n.a] + regfile[n.b];
    } else if (op == 4u /*Sub*/) {
      regfile[n.dst] = regfile[n.a] - regfile[n.b];
    } else if (op == 5u /*Mul*/) {
      regfile[n.dst] = regfile[n.a] * regfile[n.b];
    } else if (op == 6u /*Div*/) {
      regfile[n.dst] = regfile[n.a] / max(regfile[n.b], vec4(1e-6));
    } else if (op == 7u /*Min*/) {
      regfile[n.dst] = min(regfile[n.a], regfile[n.b]);
    } else if (op == 8u /*Max*/) {
      regfile[n.dst] = max(regfile[n.a], regfile[n.b]);
    } else if (op == 9u /*Clamp01*/) {
      regfile[n.dst] = clamp(regfile[n.a], vec4(0), vec4(1));
    } else if (op == 10u /*OneMinus*/) {
      regfile[n.dst] = vec4(1) - regfile[n.a];
    } else if (op == 11u /*Lerp*/) {
      vec4 t = clamp(regfile[n.c], vec4(0), vec4(1));
      regfile[n.dst] = mix(regfile[n.a], regfile[n.b], t);
    } else if (op == 12u /*Pow*/) {
      regfile[n.dst] =
          pow(max(regfile[n.a], vec4(0)), max(regfile[n.b], vec4(1e-6)));
    } else if (op == 13u /*Dot3*/) {
      float d = dot(regfile[n.a].xyz, regfile[n.b].xyz);
      regfile[n.dst] = vec4(d, d, d, d);
    } else if (op == 14u /*Normalize3*/) {
      regfile[n.dst] = vec4(normalize(regfile[n.a].xyz), 0);
    } else if (op == 1u /*Swizzle*/) {
      vec4 v = regfile[n.a];
      uint mask = n.extra;
      uint ix = (mask)&0xFFu;
      uint iy = (mask >> 8) & 0xFFu;
      uint iz = (mask >> 16) & 0xFFu;
      uint iw = (mask >> 24) & 0xFFu;
      float x = (ix == 0u) ? v.x : (ix == 1u) ? v.y : (ix == 2u) ? v.z : v.w;
      float y = (iy == 0u) ? v.x : (iy == 1u) ? v.y : (iy == 2u) ? v.z : v.w;
      float z = (iz == 0u) ? v.x : (iz == 1u) ? v.y : (iz == 2u) ? v.z : v.w;
      float w = (iw == 0u) ? v.x : (iw == 1u) ? v.y : (iw == 2u) ? v.z : v.w;
      regfile[n.dst] = vec4(x, y, z, w);
    } else if (op == 15u /*Tex2D*/) {
      vec2 uv = regfile[n.a].xy;
      uint texIndex = MAT_REMAP_TEX(n.extra);
      if (texIndex == 0xFFFFFFFFu || texIndex >= NYX_MAX_TEX) {
        regfile[n.dst] = vec4(1.0);
      } else {
        regfile[n.dst] = texture(uTex2D[NONUNIFORM(texIndex)], uv);
      }
    } else if (op == 16u /*Tex2D_SRGB*/) {
      vec2 uv = regfile[n.a].xy;
      uint texIndex = MAT_REMAP_TEX(n.extra);
      vec4 s = vec4(1.0);
      if (!(texIndex == 0xFFFFFFFFu || texIndex >= NYX_MAX_TEX))
        s = texture(uTex2D[NONUNIFORM(texIndex)], uv);
      s.rgb = srgbToLinear(s.rgb);
      regfile[n.dst] = s;
    } else if (op == 17u /*Tex2D_MRA*/) {
      vec2 uv = regfile[n.a].xy;
      uint texIndex = MAT_REMAP_TEX(n.extra);
      vec4 s = vec4(1.0);
      if (!(texIndex == 0xFFFFFFFFu || texIndex >= NYX_MAX_TEX))
        s = texture(uTex2D[NONUNIFORM(texIndex)], uv);
      // convention: R=metallic, G=roughness, B=AO
      regfile[n.dst] = vec4(s.r, s.g, s.b, 1.0);
    } else if (op == 18u /*NormalMapTS*/) {
      vec2 uv = regfile[n.a].xy;
      uint texIndex = MAT_REMAP_TEX(n.extra);
      float strength = regfile[n.b].x;
      vec3 ns = vec3(0.0, 0.0, 1.0);
      if (!(texIndex == 0xFFFFFFFFu || texIndex >= NYX_MAX_TEX))
        ns = decodeNormalTS(texture(uTex2D[NONUNIFORM(texIndex)], uv).xyz);
      ns = normalize(mix(vec3(0, 0, 1), ns, clamp(strength, 0.0, 4.0)));
      vec3 nw = normalize(TBN * ns);
      regfile[n.dst] = vec4(nw, 0);
    } else if (op == 2u /*Append*/) {
      // dst.xyz = (a.x, b.x, c.x)
      regfile[n.dst] = vec4(regfile[n.a].x, regfile[n.b].x, regfile[n.c].x, 0);
    }
    // OutputSurface nodes are ignored in shader; we use header regs.
  }

  baseColor = regfile[h.outBaseColor].xyz;
  vec3 mrAo = regfile[h.outMR].xyz;
  metallic = mrAo.x;
  roughness = mrAo.y;
  ao = mrAo.z;
  emissive = regfile[h.outEmissive].xyz;
  normalWS = normalize(regfile[h.outNormalWS].xyz);
  alpha = regfile[h.outAlpha].x;
}

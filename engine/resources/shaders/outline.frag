#version 460

layout(location = 0) out vec4 oColor;

layout(binding = 0) uniform sampler2D uSceneColor; // LDR
layout(binding = 1) uniform sampler2D uDepth;      // Depth32F
layout(binding = 2) uniform usampler2D uID;        // R32UI
layout(binding = 3) uniform usampler2D uSelMaskT;  // Selected transparent mask
uniform int u_FlipY = 0;
uniform float u_ThicknessPx = 2.0;
uniform vec3 u_ColorActive = vec3(1.0, 0.45, 0.1);
uniform vec3 u_ColorMulti = vec3(1.0, 0.85, 0.2);

layout(std430, binding = 15) readonly buffer SelectedIDs {
  uint selectedCount;
  uint activePick;
  uint ids[];
}
gSel;

bool isSelected(uint id) {
  // id==0 is "background" anyway
  for (uint i = 0; i < gSel.selectedCount; ++i) {
    if (gSel.ids[i] == id)
      return true;
  }
  return false;
}

void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  ivec2 sz = textureSize(uID, 0);
  ivec2 sp = p;
  if (u_FlipY != 0)
    sp.y = sz.y - 1 - sp.y;

  vec4 scene = texelFetch(uSceneColor, sp, 0);
  float zc = texelFetch(uDepth, sp, 0).r;
  uint idc = texelFetch(uID, sp, 0).r;
  uint mc = texelFetch(uSelMaskT, sp, 0).r;

  const bool selOpaque = isSelected(idc);
  const bool selTrans = (mc != 0u);

  if (!selOpaque && !selTrans) {
    oColor = scene;
    return;
  }

  int step = int(max(1.0, u_ThicknessPx));
  const ivec2 off[4] =
      ivec2[4](ivec2(step, 0), ivec2(-step, 0), ivec2(0, step), ivec2(0, -step));

  float edge = 0.0;
  for (int i = 0; i < 4; i++) {
    ivec2 q = clamp(p + off[i], ivec2(0), sz - ivec2(1));
    if (u_FlipY != 0)
      q.y = sz.y - 1 - q.y;

    uint idn = texelFetch(uID, q, 0).r;
    float zn = texelFetch(uDepth, q, 0).r;
    uint mn = texelFetch(uSelMaskT, q, 0).r;

    float idEdge = (selOpaque && idn != idc) ? 1.0 : 0.0;
    float zEdge = (selOpaque && abs(zn - zc) > 0.0008) ? 1.0 : 0.0;
    float mEdge = (selTrans && mc != 0u && mn == 0u) ? 1.0 : 0.0;

    edge = max(edge, max(mEdge, max(idEdge, zEdge)));
  }

  uint outlineID = selOpaque ? idc : mc;
  vec3 outline =
      (outlineID == gSel.activePick) ? u_ColorActive : u_ColorMulti;

  // Subtle fill highlight for selected surfaces
  vec3 fill = clamp(scene.rgb * 1.12 + vec3(0.04), 0.0, 1.0);
  vec3 base = mix(scene.rgb, fill, 0.45);

  // Outline on edges
  vec3 rgb = mix(base, outline, edge);
  oColor = vec4(rgb, scene.a);
}

#version 460

layout(location = 0) out vec4 oColor;

layout(binding = 0) uniform sampler2D uSceneColor; // LDR
layout(binding = 1) uniform sampler2D uDepth;      // Depth32F
layout(binding = 2) uniform usampler2D uID;        // R32UI
uniform int u_FlipY = 0;

layout(std430, binding = 15) readonly buffer SelectedIDs {
  uint selectedCount;
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

  if (!isSelected(idc)) {
    oColor = scene;
    return;
  }

  const ivec2 off[4] =
      ivec2[4](ivec2(2, 0), ivec2(-2, 0), ivec2(0, 2), ivec2(0, -2));

  float edge = 0.0;
  for (int i = 0; i < 4; i++) {
    ivec2 q = clamp(p + off[i], ivec2(0), sz - ivec2(1));
    if (u_FlipY != 0)
      q.y = sz.y - 1 - q.y;

    uint idn = texelFetch(uID, q, 0).r;
    float zn = texelFetch(uDepth, q, 0).r;

    float idEdge = (idn != idc) ? 1.0 : 0.0;
    float zEdge = (abs(zn - zc) > 0.0008) ? 1.0 : 0.0;

    edge = max(edge, max(idEdge, zEdge));
  }

  vec3 outline = vec3(1.0, 0.75, 0.2);

  // Subtle fill highlight for selected surfaces
  vec3 fill = clamp(scene.rgb * 1.12 + vec3(0.04), 0.0, 1.0);
  vec3 base = mix(scene.rgb, fill, 0.45);

  // Outline on edges
  vec3 rgb = mix(base, outline, edge);
  oColor = vec4(rgb, scene.a);
}

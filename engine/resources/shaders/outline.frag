#version 460

layout(location = 0) out vec4 oColor;

layout(binding = 0) uniform sampler2D uSceneColor; // LDR
layout(binding = 1) uniform sampler2D uDepth;      // Depth32F
layout(binding = 2) uniform usampler2D uID;        // R32UI

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

  vec4 scene = texelFetch(uSceneColor, p, 0);
  float zc = texelFetch(uDepth, p, 0).r;
  uint idc = texelFetch(uID, p, 0).r;

  if (!isSelected(idc)) {
    oColor = scene;
    return;
  }

  const ivec2 off[4] =
      ivec2[4](ivec2(2, 0), ivec2(-2, 0), ivec2(0, 2), ivec2(0, -2));

  ivec2 sz = textureSize(uID, 0);

  float edge = 0.0;
  for (int i = 0; i < 4; i++) {
    ivec2 q = clamp(p + off[i], ivec2(0), sz - ivec2(1));

    uint idn = texelFetch(uID, q, 0).r;
    float zn = texelFetch(uDepth, q, 0).r;

    float idEdge = (idn != idc) ? 1.0 : 0.0;
    float zEdge = (abs(zn - zc) > 0.0008) ? 1.0 : 0.0;

    edge = max(edge, max(idEdge, zEdge));
  }

  vec3 outline = vec3(1.0, 0.75, 0.2);

  // More visible than additive:
  vec3 rgb = mix(scene.rgb, outline, edge);
  oColor = vec4(rgb, scene.a);
}

#include "MaterialGraphCompiler.h"
#include "render/material/MaterialGraph.h"
#include "render/material/MaterialGraphVM.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Nyx {

void MaterialGraphCompiler::setError(MatCompilerError *err, const char *msg) {
  if (err)
    err->msg = msg ? msg : "MaterialGraphCompiler error";
}

const MatNode *MaterialGraphCompiler::findNode(MatNodeID id) const {
  auto it = m_nodes.find(id);
  return (it != m_nodes.end()) ? it->second : nullptr;
}

uint32_t MaterialGraphCompiler::allocReg() {
  uint32_t r = m_nextReg++;
  if (m_nextReg >= kMatVM_MaxRegs) {
    // clamp; caller will error
  }
  return r;
}

uint32_t MaterialGraphCompiler::ensureInputReg(const MatNode &node,
                                               uint32_t inputSlot,
                                               uint32_t defaultKind,
                                               const glm::vec4 &defaultV4,
                                               std::vector<GpuMatNode> &prog,
                                               MatCompilerError *err) {
  PinKey key{node.id, inputSlot};
  auto it = m_incoming.find(key);

  // Connected: compile upstream and use its output reg 0
  if (it != m_incoming.end()) {
    const MatPin from = it->second;
    const MatNode *src = findNode(from.node);
    if (!src) {
      setError(err, "Broken link: source node not found");
      return 0;
    }
    uint32_t srcReg = compileNode(*src, from.slot, prog, err);
    return srcReg;
  }

  // Not connected: emit const4 into new reg
  uint32_t dst = allocReg();
  if (dst >= kMatVM_MaxRegs) {
    setError(err, "Too many registers needed");
    return 0;
  }

  GpuMatNode cn{};
  cn.op = static_cast<uint32_t>(MatOp::Const4);
  cn.dst = dst;
  glm::uvec4 bits{0};
  bits = glm::floatBitsToUint(defaultV4);

  cn.a = bits.x;
  cn.b = bits.y;
  cn.c = bits.z;
  cn.extra = bits.w;
  prog.push_back(cn);

  return dst;
}

uint32_t MaterialGraphCompiler::compileNode(const MatNode &n, uint32_t outSlot,
                                            std::vector<GpuMatNode> &prog,
                                            MatCompilerError *err) {
  auto &info = m_info[n.id];
  if (!info.compiled) {
    info.compiled = true;
    info.outReg = glm::uvec4(0xFFFFFFFFu);
  }
  if (outSlot < 4 && info.outReg[outSlot] != 0xFFFFFFFFu) {
    return info.outReg[outSlot];
  }

  auto emit1 = [&](MatOp op, uint32_t dst, uint32_t a = 0, uint32_t b = 0,
                   uint32_t c = 0, uint32_t extra = 0) {
    GpuMatNode gn{};
    gn.op = static_cast<uint32_t>(op);
    gn.dst = dst;
    gn.a = a;
    gn.b = b;
    gn.c = c;
    gn.extra = extra;
    prog.push_back(gn);
  };

  // Common defaults
  glm::vec4 def0(0);
  glm::vec4 def1(1);

  // Inputs available to shader will be placed in fixed regs at runtime.
  // reg 0..N reserved for builtins:
  // r0 = UV0 (xy, 0, 0)
  // r1 = NormalWS (xyz, 0)
  // r2 = ViewDirWS (xyz, 0)
  // Compiler will reference these directly (no nodes needed).
  auto builtinReg = [&](MatNodeType t) -> uint32_t {
    switch (t) {
    case Nyx::MatNodeType::UV0:
      return 0;
    case Nyx::MatNodeType::NormalWS:
      return 1;
    case Nyx::MatNodeType::ViewDirWS:
      return 2;
    default:
      return 0xFFFFFFFFu;
    }
  };

  // Node compilation
  auto allocDst = [&]() -> uint32_t {
    uint32_t r = allocReg();
    if (r >= kMatVM_MaxRegs) {
      setError(err, "Material VM: out of registers");
      return 0;
    }
    return r;
  };

  switch (n.type) {
  // Builtins
  case Nyx::MatNodeType::UV0:
  case Nyx::MatNodeType::NormalWS:
  case Nyx::MatNodeType::ViewDirWS: {
    uint32_t br = builtinReg(n.type);
    info.outReg[0] = br;
    return br;
  }

  // Consts
  case Nyx::MatNodeType::ConstFloat: {
    glm::vec4 v{n.f.x, 0, 0, 0};
    // overwrite dst const
    uint32_t dst = allocDst();
    GpuMatNode cn{};
    cn.op = static_cast<uint32_t>(MatOp::Const4);
    cn.dst = dst;
    glm::uvec4 bits = glm::floatBitsToUint(v);
    cn.a = bits.x;
    cn.b = bits.y;
    cn.c = bits.z;
    cn.extra = bits.w;
    prog.push_back(cn);
    info.outReg.x = dst;
    return dst;
  }
  case Nyx::MatNodeType::ConstVec3:
  case Nyx::MatNodeType::ConstColor:
  case Nyx::MatNodeType::ConstVec4: {
    glm::vec4 v{n.f};
    if (n.type != MatNodeType::ConstVec4)
      v.w = 1.0f;
    uint32_t dst = allocDst();
    GpuMatNode cn{};
    cn.op = static_cast<uint32_t>(MatOp::Const4);
    cn.dst = dst;
    glm::uvec4 bits = glm::floatBitsToUint(v);
    cn.a = bits.x;
    cn.b = bits.y;
    cn.c = bits.z;
    cn.extra = bits.w;
    prog.push_back(cn);
    info.outReg.x = dst;
    return dst;
  }

  // Textures
  case Nyx::MatNodeType::Texture2D: {
    // inputs: UV (slot0)
    // params: u[0]=texIndex, u[1]=srgb(0/1)
    uint32_t dst = allocDst();
    glm::vec4 uvDef{0};
    uint32_t uv = ensureInputReg(n, 0, 0, uvDef, prog, err);
    uint32_t texIndex = n.u.x;
    uint32_t srgb = n.u.y ? 1u : 0u;
    emit1(srgb ? MatOp::Tex2D_SRGB : MatOp::Tex2D, dst, uv, 0, 0, texIndex);
    info.outReg.x = dst;
    return dst;
  }
  case Nyx::MatNodeType::TextureMRA: {
    uint32_t dst = allocDst();
    glm::vec4 uvDef{0};
    uint32_t uv = ensureInputReg(n, 0, 0, uvDef, prog, err);
    uint32_t texIndex = n.u.x;
    emit1(MatOp::Tex2D_MRA, dst, uv, 0, 0, texIndex);
    info.outReg.x = dst;
    return dst;
  }
  case Nyx::MatNodeType::NormalMap: {
    // inputs: UV(slot0), NormalWS(slot1) optionally ignored, strength(slot2)
    // params: u[0]=texIndex
    uint32_t dst = allocDst();
    glm::vec4 uvDef{0};
    glm::vec4 strDef{1, 0, 0, 0};
    uint32_t uv = ensureInputReg(n, 0, 0, uvDef, prog, err);
    uint32_t str = ensureInputReg(n, 2, 0, strDef, prog, err);
    uint32_t texIndex = n.u.x;
    // NormalMapTS op expects: a=uvReg, b=strengthReg, extra=texIndex
    emit1(MatOp::NormalMapTS, dst, uv, str, 0, texIndex);
    info.outReg.x = dst;
    return dst;
  }

  // Math unary/binary
  case Nyx::MatNodeType::Add:
  case Nyx::MatNodeType::Sub:
  case Nyx::MatNodeType::Mul:
  case Nyx::MatNodeType::Div:
  case Nyx::MatNodeType::Min:
  case Nyx::MatNodeType::Max: {
    uint32_t op = static_cast<uint32_t>(MatOp::Add);
    if (n.type == MatNodeType::Sub)
      op = static_cast<uint32_t>(MatOp::Sub);
    if (n.type == MatNodeType::Mul)
      op = static_cast<uint32_t>(MatOp::Mul);
    if (n.type == MatNodeType::Div)
      op = static_cast<uint32_t>(MatOp::Div);
    if (n.type == MatNodeType::Min)
      op = static_cast<uint32_t>(MatOp::Min);
    if (n.type == MatNodeType::Max)
      op = static_cast<uint32_t>(MatOp::Max);

    uint32_t dst = allocDst();
    uint32_t a = ensureInputReg(n, 0, 0, def0, prog, err);
    uint32_t b = ensureInputReg(n, 1, 0, def0, prog, err);
    emit1(static_cast<MatOp>(op), dst, a, b);
    info.outReg.x = dst;
    return dst;
  }
  case MatNodeType::Clamp01: {
    uint32_t dst = allocDst();
    uint32_t a = ensureInputReg(n, 0, 0, def0, prog, err);
    emit1(MatOp::Clamp01, dst, a);
    info.outReg[0] = dst;
    return dst;
  }
  case MatNodeType::OneMinus: {
    uint32_t dst = allocDst();
    uint32_t a = ensureInputReg(n, 0, 0, def0, prog, err);
    emit1(MatOp::OneMinus, dst, a);
    info.outReg[0] = dst;
    return dst;
  }
  case MatNodeType::Lerp: {
    uint32_t dst = allocDst();
    uint32_t a = ensureInputReg(n, 0, 0, def0, prog, err);
    uint32_t b = ensureInputReg(n, 1, 0, def0, prog, err);
    uint32_t t = ensureInputReg(n, 2, 0, def0, prog, err);
    emit1(MatOp::Lerp, dst, a, b, t);
    info.outReg[0] = dst;
    return dst;
  }
  case MatNodeType::Pow: {
    uint32_t dst = allocDst();
    uint32_t a = ensureInputReg(n, 0, 0, def0, prog, err);
    uint32_t b = ensureInputReg(n, 1, 0, def1, prog, err);
    emit1(MatOp::Pow, dst, a, b);
    info.outReg[0] = dst;
    return dst;
  }
  case MatNodeType::Dot3: {
    uint32_t dst = allocDst();
    uint32_t a = ensureInputReg(n, 0, 0, def0, prog, err);
    uint32_t b = ensureInputReg(n, 1, 0, def0, prog, err);
    emit1(MatOp::Dot3, dst, a, b);
    info.outReg[0] = dst;
    return dst;
  }
  case MatNodeType::Normalize3: {
    uint32_t dst = allocDst();
    uint32_t a = ensureInputReg(n, 0, 0, def0, prog, err);
    emit1(MatOp::Normalize3, dst, a);
    info.outReg[0] = dst;
    return dst;
  }

  case MatNodeType::Swizzle: {
    uint32_t dst = allocDst();
    uint32_t a = ensureInputReg(n, 0, 0, def0, prog, err);
    emit1(MatOp::Swizzle, dst, a, 0, 0, n.u.x);
    info.outReg[0] = dst;
    return dst;
  }

  case MatNodeType::Split: {
    if (outSlot > 3)
      outSlot = 0;
    if (info.outReg[outSlot] != 0u)
      return info.outReg[outSlot];
    uint32_t dst = allocDst();
    uint32_t a = ensureInputReg(n, 0, 0, def0, prog, err);
    const uint32_t mask =
        (outSlot & 0xFF) | ((outSlot & 0xFF) << 8) |
        ((outSlot & 0xFF) << 16) | ((outSlot & 0xFF) << 24);
    emit1(MatOp::Swizzle, dst, a, 0, 0, mask);
    info.outReg[outSlot] = dst;
    return dst;
  }

  case MatNodeType::Channel: {
    uint32_t dst = allocDst();
    uint32_t a = ensureInputReg(n, 0, 0, def0, prog, err);
    const uint32_t ch = n.u.x & 0xFF;
    const uint32_t mask =
        ch | (ch << 8) | (ch << 16) | (ch << 24);
    emit1(MatOp::Swizzle, dst, a, 0, 0, mask);
    info.outReg[0] = dst;
    return dst;
  }

  // Output
  case Nyx::MatNodeType::SurfaceOutput: {
    // Inputs:
    // 0 baseColor (vec3)
    // 1 metallic (float)
    // 2 roughness (float)
    // 3 normalWS (vec3) (default uses builtin NormalWS)
    // 4 ao (float)
    // 5 emissive (vec3)
    // 6 alpha (float)
    uint32_t base = ensureInputReg(n, 0, 0, def1, prog, err);
    glm::vec4 mDef{0};
    glm::vec4 rDef{0.5f, 0, 0, 0};
    glm::vec4 aoDef{1, 0, 0, 0};
    glm::vec4 eDef{0};
    glm::vec4 aDef{1, 0, 0, 0};

    uint32_t metallic = ensureInputReg(n, 1, 0, mDef, prog, err);
    uint32_t rough = ensureInputReg(n, 2, 0, rDef, prog, err);

    // default normal = builtin NormalWS reg=1
    uint32_t normal = 1;
    {
      PinKey key{n.id, 3};
      auto it = m_incoming.find(key);
      if (it != m_incoming.end()) {
        const MatNode *src = findNode(it->second.node);
        if (!src) {
          setError(err, "SurfaceOutput normal source missing");
          return 0;
        }
        normal = compileNode(*src, 0, prog, err);
      }
    }

    uint32_t ao = ensureInputReg(n, 4, 0, aoDef, prog, err);
    uint32_t emis = ensureInputReg(n, 5, 0, eDef, prog, err);
    uint32_t alpha = ensureInputReg(n, 6, 0, aDef, prog, err);

    emit1(MatOp::OutputSurface, 0, base, metallic, rough, normal);
    emit1(MatOp::OutputSurface, 0, ao, emis, alpha, 0xFFFFFFFFu);

    m_outSet = true;
    m_outBase = base;
    m_outMetal = metallic;
    m_outRough = rough;
    m_outNormal = normal;
    m_outAO = ao;
    m_outEmis = emis;
    m_outAlpha = alpha;

    info.outReg[0] = 0;
    return 0;
  }

  default:
    setError(err, "Unsupported node type in compiler");
    info.outReg[0] = 0;
    return 0;
  }
}

bool MaterialGraphCompiler::compile(const MaterialGraph &g,
                                    CompiledMaterialGraph &out,
                                    MatCompilerError *err) {
  out = {};

  m_nodes.clear();
  m_incoming.clear();
  m_info.clear();
  m_outSet = false;

  // Reserve builtins regs 0..2
  m_nextReg = 3;

  // Build node map
  for (auto &n : g.nodes) {
    m_nodes[n.id] = &n;
  }

  // Build incoming link map (to -> from)
  for (auto &l : g.links) {
    PinKey key{l.to.node, l.to.slot};
    m_incoming[key] = l.from; // last wins
  }

  MatNodeID outId = g.findSurfaceOutput();
  if (outId == 0) {
    setError(err, "MaterialGraph missing SurfaceOutput node");
    return false;
  }

  const MatNode *outNode = findNode(outId);
  if (!outNode) {
    setError(err, "SurfaceOutput node not found");
    return false;
  }

  std::vector<GpuMatNode> prog;
  prog.reserve(g.nodes.size() * 2);

  // Compile output node (recursively compiles dependencies)
  (void)compileNode(*outNode, 0, prog, err);

  if (!err->msg.empty())
    return false;
  if (m_nextReg >= kMatVM_MaxRegs) {
    setError(err, "Material VM: exceeded max regs");
    return false;
  }
  if (prog.size() >= kMatVM_MaxNodes) {
    setError(err, "Material VM: exceeded max nodes");
  }

  uint32_t base = 0, metal = 0, rough = 0, normal = 1, ao = 0, emis = 0,
           alpha = 0;
  if (m_outSet) {
    base = m_outBase;
    metal = m_outMetal;
    rough = m_outRough;
    normal = m_outNormal;
    ao = m_outAO;
    emis = m_outEmis;
    alpha = m_outAlpha;
  } else {
    int found = 0;
    for (int i = (int)prog.size() - 1; i >= 0; --i) {
      if (static_cast<MatOp>(prog[i].op) != MatOp::OutputSurface)
        continue;
      const auto &n = prog[i];
      if (found == 0) { // second one (tail)
        ao = n.a, emis = n.b, alpha = n.c;
        found++;
      } else {
        base = n.a;
        metal = n.b;
        rough = n.c;
        normal = n.extra;
        found++;
        break;
      }
    }
    if (found < 2) {
      setError(err, "MaterialGraph: failed to extract outputs");
      return false;
    }
  }

  out.header.outBaseColor = base;
  uint32_t mrReg = allocReg();
  if (mrReg >= kMatVM_MaxRegs) {
    setError(err, "Material VM: out of regs (MR)");
    return false;
  }
  {
    // Use Append with a=metalReg b=roughReg c=aoReg extra unused
    GpuMatNode an{};
    an.op = static_cast<uint32_t>(MatOp::Append);
    an.dst = mrReg;
    an.a = metal;
    an.b = rough;
    an.c = ao;
    an.extra = 0;
    prog.push_back(an);
  }

  out.header.outMR = mrReg;
  out.header.outNormalWS = normal;
  out.header.outEmissive = emis;
  out.header.outAlpha = alpha;

  out.header.alphaMode = static_cast<uint32_t>(g.alphaMode);
  out.header.alphaCutoff = g.alphaCutoff;

  out.header.nodeOffset = 0;
  out.header.nodeCount = prog.size();

  out.nodes = std::move(prog);
  return true;
}

} // namespace Nyx

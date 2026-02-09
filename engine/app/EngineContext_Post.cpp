#include "EngineContext.h"

#include "render/filters/LUT3DLoader.h"

#include <glad/glad.h>

namespace Nyx {

void EngineContext::initPostFilters() {
  m_filterRegistry.clear();
  m_filterRegistry.registerBuiltins();
  m_filterRegistry.finalize();

  m_filterStack.init(m_filterRegistry);

  if (!m_postLUT3D) {
    glCreateTextures(GL_TEXTURE_3D, 1, &m_postLUT3D);
    glTextureParameteri(m_postLUT3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_postLUT3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_postLUT3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_postLUT3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_postLUT3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  }
  const int lutSize = 16;
  std::vector<float> lut;
  lut.resize((size_t)lutSize * lutSize * lutSize * 3u);
  for (int b = 0; b < lutSize; ++b) {
    for (int g = 0; g < lutSize; ++g) {
      for (int r = 0; r < lutSize; ++r) {
        const float rf = (float)r / (float)(lutSize - 1);
        const float gf = (float)g / (float)(lutSize - 1);
        const float bf = (float)b / (float)(lutSize - 1);
        const size_t idx =
            ((size_t)b * lutSize * lutSize + (size_t)g * lutSize + (size_t)r) *
            3u;
        lut[idx + 0] = rf;
        lut[idx + 1] = gf;
        lut[idx + 2] = bf;
      }
    }
  }
  glTextureStorage3D(m_postLUT3D, 1, GL_RGB16F, lutSize, lutSize, lutSize);
  glTextureSubImage3D(m_postLUT3D, 0, 0, 0, 0, lutSize, lutSize, lutSize,
                      GL_RGB, GL_FLOAT, lut.data());
  m_postLUTs.clear();
  m_postLUTIndex.clear();
  m_postLUTs.push_back(m_postLUT3D);
  m_postLUTPaths.clear();
  m_postLUTPaths.emplace_back("");
  m_postLUTSizes.clear();
  m_postLUTSizes.push_back(lutSize);

  m_postGraph = PostGraph();
  const FilterNode exp = m_filterRegistry.makeNode(1);
  const FilterNode con = m_filterRegistry.makeNode(2);
  const FilterNode sat = m_filterRegistry.makeNode(3);

  auto defaultsFrom = [](const FilterRegistry &reg, FilterTypeId id) {
    std::vector<float> out;
    const FilterTypeInfo *t = reg.find(id);
    if (!t)
      return out;
    out.reserve(t->paramCount);
    for (uint32_t i = 0; i < t->paramCount; ++i)
      out.push_back(t->params[i].defaultValue);
    return out;
  };

  m_postGraph.addFilter(1, exp.label.c_str(),
                        defaultsFrom(m_filterRegistry, 1));
  m_postGraph.addFilter(2, con.label.c_str(),
                        defaultsFrom(m_filterRegistry, 2));
  m_postGraph.addFilter(3, sat.label.c_str(),
                        defaultsFrom(m_filterRegistry, 3));

  m_postGraphDirty = true;
  syncFilterGraphFromPostGraph();
}

void EngineContext::updatePostFilters() {
  if (m_postGraphDirty)
    syncFilterGraphFromPostGraph();
  m_filterStack.updateIfDirty(m_filterGraph);
}

uint32_t EngineContext::postLUTTexture(uint32_t idx) const {
  if (idx >= m_postLUTs.size())
    return m_postLUT3D;
  return m_postLUTs[idx];
}

uint32_t EngineContext::postLUTSize(uint32_t idx) const {
  if (idx >= m_postLUTSizes.size())
    return 0;
  return m_postLUTSizes[idx];
}

uint32_t EngineContext::ensurePostLUT3D(const std::string &path) {
  if (path.empty())
    return 0u;
  auto it = m_postLUTIndex.find(path);
  if (it != m_postLUTIndex.end())
    return it->second;

  if (m_postLUTs.size() >= 8)
    return 0u;

  LUT3DData data{};
  std::string err;
  if (!loadCubeLUT3D(path, data, err))
    return 0u;

  uint32_t tex = 0;
  glCreateTextures(GL_TEXTURE_3D, 1, &tex);
  glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glTextureStorage3D(tex, 1, GL_RGB16F, (GLsizei)data.size, (GLsizei)data.size,
                     (GLsizei)data.size);
  glTextureSubImage3D(tex, 0, 0, 0, 0, (GLsizei)data.size, (GLsizei)data.size,
                      (GLsizei)data.size, GL_RGB, GL_FLOAT, data.rgb.data());

  const uint32_t idx = (uint32_t)m_postLUTs.size();
  m_postLUTs.push_back(tex);
  m_postLUTPaths.push_back(path);
  m_postLUTSizes.push_back(data.size);
  m_postLUTIndex.emplace(path, idx);
  return idx;
}

bool EngineContext::reloadPostLUT3D(const std::string &path) {
  if (path.empty())
    return false;
  auto it = m_postLUTIndex.find(path);
  if (it == m_postLUTIndex.end())
    return false;
  const uint32_t idx = it->second;
  if (idx == 0 || idx >= m_postLUTs.size())
    return false;

  LUT3DData data{};
  std::string err;
  if (!loadCubeLUT3D(path, data, err))
    return false;

  uint32_t &tex = m_postLUTs[idx];
  if (m_postLUTSizes[idx] != data.size) {
    if (tex)
      glDeleteTextures(1, &tex);
    glCreateTextures(GL_TEXTURE_3D, 1, &tex);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureStorage3D(tex, 1, GL_RGB16F, (GLsizei)data.size,
                       (GLsizei)data.size, (GLsizei)data.size);
  }

  glTextureSubImage3D(tex, 0, 0, 0, 0, (GLsizei)data.size, (GLsizei)data.size,
                      (GLsizei)data.size, GL_RGB, GL_FLOAT, data.rgb.data());
  m_postLUTSizes[idx] = data.size;
  return true;
}

bool EngineContext::clearPostLUT(uint32_t idx) {
  if (idx == 0 || idx >= m_postLUTs.size())
    return false;
  uint32_t &tex = m_postLUTs[idx];
  if (tex) {
    glDeleteTextures(1, &tex);
    tex = m_postLUT3D;
  }
  if (idx < m_postLUTPaths.size())
    m_postLUTPaths[idx].clear();
  if (idx < m_postLUTSizes.size())
    m_postLUTSizes[idx] = m_postLUTSizes[0];
  for (auto it = m_postLUTIndex.begin(); it != m_postLUTIndex.end();) {
    if (it->second == idx)
      it = m_postLUTIndex.erase(it);
    else
      ++it;
  }
  return true;
}

void EngineContext::syncFilterGraphFromPostGraph() {
  std::vector<PGNodeID> order;
  const PGCompileError err = m_postGraph.buildChainOrder(order);
  if (!err.ok) {
    m_filterGraph.clear();
    m_postGraphDirty = false;
    return;
  }

  m_filterGraph.clear();
  for (PGNodeID id : order) {
    PGNode *n = m_postGraph.findNode(id);
    if (!n || n->kind != PGNodeKind::Filter)
      continue;

    const FilterTypeInfo *ti =
        m_filterRegistry.find(static_cast<FilterTypeId>(n->typeID));
    if (!ti)
      continue;

    FilterNode fn = m_filterRegistry.makeNode(ti->id);
    fn.enabled = n->enabled;
    fn.label = n->name;

    const uint32_t pc =
        std::min<uint32_t>(ti->paramCount, FilterNode::kMaxParams);
    for (uint32_t i = 0; i < pc; ++i) {
      if (i < n->params.size())
        fn.params[i] = n->params[i];
    }
    if (ti->name && std::string(ti->name) == "LUT") {
      const uint32_t lutIdx = ensurePostLUT3D(n->lutPath);
      if (pc > 1)
        fn.params[1] = (float)lutIdx;
    }

    m_filterGraph.addNode(std::move(fn));
  }

  m_postGraphDirty = false;
}

} // namespace Nyx

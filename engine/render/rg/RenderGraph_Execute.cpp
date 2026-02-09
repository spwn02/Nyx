#include "RenderGraph.h"

#include "core/Assert.h"
#include "core/Log.h"
#include "render/rg/RenderPassContext.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <unordered_map>

#include <glad/glad.h>

namespace Nyx {

static bool isWriteAccess(RenderAccess a) {
  return hasAccess(a, RenderAccess::ColorWrite) ||
         hasAccess(a, RenderAccess::DepthWrite) ||
         hasAccess(a, RenderAccess::ImageWrite) ||
         hasAccess(a, RenderAccess::SSBOWrite);
}

static bool isTextureAccess(RenderAccess a) {
  return hasAccess(a, RenderAccess::ColorWrite) ||
         hasAccess(a, RenderAccess::DepthWrite) ||
         hasAccess(a, RenderAccess::SampledRead) ||
         hasAccess(a, RenderAccess::ImageRead) ||
         hasAccess(a, RenderAccess::ImageWrite);
}

static bool isSSBOAccess(RenderAccess a) {
  return hasAccess(a, RenderAccess::SSBORead) ||
         hasAccess(a, RenderAccess::SSBOWrite);
}

static bool isBufferAccess(RenderAccess a) {
  return isSSBOAccess(a) || hasAccess(a, RenderAccess::UBORead);
}

static GLbitfield barrierForTransition(RenderAccess prev, RenderAccess next) {
  GLbitfield bits = 0;
  if (isSSBOAccess(prev) && hasAccess(prev, RenderAccess::SSBOWrite) &&
      isBufferAccess(next)) {
    bits |= GL_SHADER_STORAGE_BARRIER_BIT;
    if (hasAccess(next, RenderAccess::UBORead))
      bits |= GL_UNIFORM_BARRIER_BIT;
  }

  if (isTextureAccess(prev)) {
    const bool prevColorDepthWrite =
        hasAccess(prev, RenderAccess::ColorWrite) ||
        hasAccess(prev, RenderAccess::DepthWrite);
    const bool prevImageWrite = hasAccess(prev, RenderAccess::ImageWrite);

    if (prevColorDepthWrite) {
      if (hasAccess(next, RenderAccess::SampledRead))
        bits |= GL_TEXTURE_FETCH_BARRIER_BIT;
      if (hasAccess(next, RenderAccess::ImageRead) ||
          hasAccess(next, RenderAccess::ImageWrite))
        bits |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
      if (isTextureAccess(next))
        bits |= GL_FRAMEBUFFER_BARRIER_BIT;
    }

    if (prevImageWrite) {
      if (hasAccess(next, RenderAccess::SampledRead))
        bits |= GL_TEXTURE_FETCH_BARRIER_BIT;
      if (hasAccess(next, RenderAccess::ImageRead) ||
          hasAccess(next, RenderAccess::ImageWrite))
        bits |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
      if (hasAccess(next, RenderAccess::ColorWrite) ||
          hasAccess(next, RenderAccess::DepthWrite))
        bits |= GL_FRAMEBUFFER_BARRIER_BIT;
    }
  }

  return bits;
}

static RGTexDesc resolveTextureDesc(const RenderPassContext &ctx,
                                    const RenderTextureDesc &desc) {
  RGTexDesc out{};
  out.fmt = desc.format;
  out.usage = desc.usage;
  out.mips = std::max(1u, desc.mipCount);
  out.layers = std::max(1u, desc.layers);

  switch (desc.extent.kind) {
  case RenderExtentKind::Window:
    out.w = ctx.windowWidth;
    out.h = ctx.windowHeight;
    break;
  case RenderExtentKind::Viewport:
    out.w = ctx.viewportWidth;
    out.h = ctx.viewportHeight;
    break;
  case RenderExtentKind::Framebuffer:
    out.w = ctx.fbWidth;
    out.h = ctx.fbHeight;
    break;
  case RenderExtentKind::Explicit:
  default:
    out.w = desc.extent.w;
    out.h = desc.extent.h;
    break;
  }

  if (out.w == 0)
    out.w = 1;
  if (out.h == 0)
    out.h = 1;
  if (out.layers == 0)
    out.layers = 1;

  return out;
}

void RenderGraph::execute(const RenderPassContext &ctx, RGResources &rg) {
  if (m_passes.empty())
    return;

  const uint32_t passCount = (uint32_t)m_passes.size();
  std::vector<std::vector<uint32_t>> edges(passCount);
  std::vector<uint32_t> indegree(passCount, 0);

  auto resourceCount = m_blackboard.textureCount();
  auto bufferCount = m_blackboard.bufferCount();
  std::vector<int32_t> lastWriter(resourceCount, -1);
  std::vector<int32_t> lastAccess(resourceCount, -1);
  lastWriter.assign(resourceCount, -1);
  lastAccess.assign(resourceCount, -1);

  std::vector<int32_t> lastBufWriter(bufferCount, -1);
  std::vector<int32_t> lastBufAccess(bufferCount, -1);

  if (m_validate) {
    if (resourceCount > 0) {
      struct Usage {
        bool used = false;
        bool read = false;
        bool written = false;
      };
      std::vector<Usage> usage(resourceCount);

      for (const auto &p : m_passes) {
        for (const auto &u : p.texUses) {
          const uint32_t res = u.first;
          if (res >= resourceCount)
            continue;
          const RenderAccess access = u.second;
          const RenderTextureDesc &desc =
              m_blackboard.textureDesc(RGTextureRef{res + 1});

          usage[res].used = true;
          if (isWriteAccess(access))
            usage[res].written = true;
          if (!isWriteAccess(access))
            usage[res].read = true;

          if (hasAccess(access, RenderAccess::ColorWrite) &&
              !hasUsage(desc.usage, RGTexUsage::ColorAttach)) {
            Log::Warn(
                "RG: pass '{}' writes color to '{}' without ColorAttach usage",
                p.name, m_blackboard.textureName(RGTextureRef{res + 1}));
          }
          if (hasAccess(access, RenderAccess::DepthWrite) &&
              !hasUsage(desc.usage, RGTexUsage::DepthAttach)) {
            Log::Warn(
                "RG: pass '{}' writes depth to '{}' without DepthAttach usage",
                p.name, m_blackboard.textureName(RGTextureRef{res + 1}));
          }
          if (hasAccess(access, RenderAccess::SampledRead) &&
              !hasUsage(desc.usage, RGTexUsage::Sampled)) {
            Log::Warn("RG: pass '{}' samples '{}' without Sampled usage", p.name,
                      m_blackboard.textureName(RGTextureRef{res + 1}));
          }
          if ((hasAccess(access, RenderAccess::ImageRead) ||
               hasAccess(access, RenderAccess::ImageWrite)) &&
              !hasUsage(desc.usage, RGTexUsage::Image)) {
            Log::Warn("RG: pass '{}' uses image '{}' without Image usage",
                      p.name, m_blackboard.textureName(RGTextureRef{res + 1}));
          }
        }
      }

      for (uint32_t i = 0; i < resourceCount; ++i) {
        if (!usage[i].used) {
          Log::Warn("RG: texture '{}' declared but never used",
                    m_blackboard.textureName(RGTextureRef{i + 1}));
          continue;
        }
        if (usage[i].read && !usage[i].written) {
          Log::Warn("RG: texture '{}' is read but never written",
                    m_blackboard.textureName(RGTextureRef{i + 1}));
        }
      }
    }
  }

  for (uint32_t i = 0; i < passCount; ++i) {
    for (const auto &use : m_passes[i].texUses) {
      const uint32_t res = use.first;
      const RenderAccess access = use.second;
      if (res >= resourceCount)
        continue;

      const bool write = isWriteAccess(access);
      if (write) {
        if (lastAccess[res] >= 0) {
          edges[(uint32_t)lastAccess[res]].push_back(i);
        }
        lastWriter[res] = (int32_t)i;
        lastAccess[res] = (int32_t)i;
      } else {
        if (lastWriter[res] >= 0) {
          edges[(uint32_t)lastWriter[res]].push_back(i);
        }
        lastAccess[res] = (int32_t)i;
      }
    }
    for (const auto &use : m_passes[i].bufUses) {
      const uint32_t res = use.first;
      const RenderAccess access = use.second;
      if (res >= bufferCount)
        continue;

      const bool write = isWriteAccess(access);
      if (write) {
        if (lastBufAccess[res] >= 0) {
          edges[(uint32_t)lastBufAccess[res]].push_back(i);
        }
        lastBufWriter[res] = (int32_t)i;
        lastBufAccess[res] = (int32_t)i;
      } else {
        if (lastBufWriter[res] >= 0) {
          edges[(uint32_t)lastBufWriter[res]].push_back(i);
        }
        lastBufAccess[res] = (int32_t)i;
      }
    }
  }

  for (uint32_t u = 0; u < passCount; ++u) {
    for (uint32_t v : edges[u])
      indegree[v]++;
  }

  std::vector<uint32_t> order;
  order.reserve(passCount);

  std::vector<uint32_t> ready;
  ready.reserve(passCount);
  for (uint32_t i = 0; i < passCount; ++i) {
    if (indegree[i] == 0)
      ready.push_back(i);
  }

  while (!ready.empty()) {
    auto it = std::min_element(ready.begin(), ready.end(),
                               [&](uint32_t a, uint32_t b) {
                                 return m_passes[a].order < m_passes[b].order;
                               });
    uint32_t u = *it;
    ready.erase(it);
    order.push_back(u);
    for (uint32_t v : edges[u]) {
      if (--indegree[v] == 0)
        ready.push_back(v);
    }
  }

  if (order.size() != passCount) {
    NYX_ASSERT(false, "RenderGraph cycle detected");
    return;
  }

  if (m_debugEnabled) {
    m_lastOrder = order;
    m_lastEdges = edges;
  }

  struct Lifetime {
    uint32_t first = UINT32_MAX;
    uint32_t last = 0;
  };

  std::vector<Lifetime> lifetimes(resourceCount);
  for (uint32_t i = 0; i < order.size(); ++i) {
    const auto &p = m_passes[order[i]];
    for (const auto &use : p.texUses) {
      const uint32_t res = use.first;
      auto &lt = lifetimes[res];
      if (i < lt.first)
        lt.first = i;
      if (i > lt.last)
        lt.last = i;
    }
  }

  if (m_debugEnabled) {
    m_lastLifetimes.clear();
    m_lastLifetimes.reserve(resourceCount);
    for (uint32_t i = 0; i < resourceCount; ++i) {
      m_lastLifetimes.emplace_back(lifetimes[i].first, lifetimes[i].last);
    }
    m_lastResolved.assign(resourceCount, RGTexDesc{});
  }

  struct ActiveTex {
    uint32_t res = 0;
    uint32_t last = 0;
  };
  std::vector<ActiveTex> active;
  active.reserve(resourceCount);

  std::unordered_map<uint32_t, RGHandle> assigned;
  assigned.reserve(resourceCount);

  for (uint32_t i = 0; i < order.size(); ++i) {
    active.erase(std::remove_if(active.begin(), active.end(), [&](ActiveTex &a) {
                    if (a.last < i) {
                      RGHandle h = assigned[a.res];
                      const RGTexDesc desc = rg.desc(h);
                      m_aliasPool.push_back(AliasEntry{h, desc});
                      return true;
                    }
                    return false;
                  }),
                 active.end());

    const auto &p = m_passes[order[i]];
    for (const auto &use : p.texUses) {
      const uint32_t res = use.first;
      if (assigned.find(res) != assigned.end())
        continue;

      const RenderTextureDesc &rt = m_blackboard.textureDesc(RGTextureRef{res + 1});
      const RGTexDesc desc = resolveTextureDesc(ctx, rt);

      RGHandle handle = InvalidRG;
      for (auto it = m_aliasPool.begin(); it != m_aliasPool.end(); ++it) {
        if (it->desc == desc) {
          handle = it->handle;
          m_aliasPool.erase(it);
          break;
        }
      }

      if (handle == InvalidRG) {
        handle = rg.allocateTex(m_blackboard.textureName(RGTextureRef{res + 1}).c_str(), desc);
      }

      assigned[res] = handle;
      m_blackboard.setTextureHandle(RGTextureRef{res + 1}, handle);
      active.push_back(ActiveTex{res, lifetimes[res].last});
      if (m_debugEnabled && res < m_lastResolved.size())
        m_lastResolved[res] = desc;
    }
  }

  std::unordered_map<uint32_t, RenderAccess> lastAccessByRes;
  lastAccessByRes.reserve(resourceCount);
  std::unordered_map<uint32_t, RenderAccess> lastAccessByBuf;
  lastAccessByBuf.reserve(bufferCount);

  std::vector<RGBufHandle> bufHandles;
  bufHandles.reserve(bufferCount);
  for (uint32_t i = 0; i < bufferCount; ++i) {
    const RGBufferDesc &desc = m_blackboard.bufferDesc(RGBufferRef{i + 1});
    RGBufHandle handle = InvalidRG;
    if (!m_blackboard.isExternalBuffer(RGBufferRef{i + 1})) {
      handle =
          rg.acquireBuf(m_blackboard.bufferName(RGBufferRef{i + 1}).c_str(),
                        desc);
      m_blackboard.setBufferHandle(RGBufferRef{i + 1}, handle);
    }
    bufHandles.push_back(handle);
  }

  for (uint32_t idx : order) {
    GLbitfield barrierBits = 0;
    for (const auto &use : m_passes[idx].texUses) {
      const uint32_t res = use.first;
      const RenderAccess access = use.second;
      auto it = lastAccessByRes.find(res);
      if (it != lastAccessByRes.end()) {
        barrierBits |= barrierForTransition(it->second, access);
      }
    }
    for (const auto &use : m_passes[idx].bufUses) {
      const uint32_t res = use.first;
      const RenderAccess access = use.second;
      auto it = lastAccessByBuf.find(res);
      if (it != lastAccessByBuf.end()) {
        barrierBits |= barrierForTransition(it->second, access);
      }
    }

    if (barrierBits != 0)
      glMemoryBarrier(barrierBits);

    if (m_passes[idx].exec)
      m_passes[idx].exec(ctx, m_blackboard, rg);

    for (const auto &use : m_passes[idx].texUses) {
      lastAccessByRes[use.first] = use.second;
    }
    for (const auto &use : m_passes[idx].bufUses) {
      lastAccessByBuf[use.first] = use.second;
    }
  }

  for (const auto &a : active) {
    RGHandle h = assigned[a.res];
    const RGTexDesc desc = rg.desc(h);
    m_aliasPool.push_back(AliasEntry{h, desc});
  }

  for (uint32_t i = 0; i < bufferCount; ++i) {
    if (m_blackboard.isExternalBuffer(RGBufferRef{i + 1}))
      continue;
    const RGBufHandle h = bufHandles[i];
    if (h != InvalidRG)
      rg.releaseBuf(h);
  }

  if (m_debugEnabled) {
    dumpGraphDot();
    if (m_debugDumpLifetimes)
      dumpResourceLifetimes();
  }
}

void RenderGraph::enableDebug(const std::string &dotPath,
                              bool dumpLifetimes) {
  m_debugEnabled = true;
  m_debugDumpLifetimes = dumpLifetimes;
  m_debugDotPath = dotPath;
}

void RenderGraph::dumpGraphDot() const {
  if (!m_debugEnabled || m_debugDotPath.empty())
    return;

  std::ofstream out(m_debugDotPath, std::ios::binary);
  if (!out.is_open()) {
    Log::Warn("RenderGraph: failed to open DOT path: {}", m_debugDotPath);
    return;
  }

  out << "digraph RenderGraph {\n";
  out << "  rankdir=LR;\n";
  for (uint32_t i = 0; i < m_passes.size(); ++i) {
    out << "  p" << i << " [label=\"" << m_passes[i].name << "\"];\n";
  }
  for (uint32_t u = 0; u < m_lastEdges.size(); ++u) {
    for (uint32_t v : m_lastEdges[u]) {
      out << "  p" << u << " -> p" << v << ";\n";
    }
  }
  out << "}\n";
}

static const char *fmtName(RGFormat fmt) {
  switch (fmt) {
  case RGFormat::RGBA16F:
    return "RGBA16F";
  case RGFormat::RGBA8:
    return "RGBA8";
  case RGFormat::Depth32F:
    return "Depth32F";
  case RGFormat::R32UI:
    return "R32UI";
  case RGFormat::R32F:
    return "R32F";
  default:
    return "Unknown";
  }
}

void RenderGraph::dumpResourceLifetimes() const {
  if (!m_debugEnabled)
    return;

  for (uint32_t i = 0; i < m_blackboard.textureCount(); ++i) {
    RGTextureRef ref{i + 1};
    const auto &name = m_blackboard.textureName(ref);
    const auto &desc = m_lastResolved.size() > i ? m_lastResolved[i]
                                                 : RGTexDesc{};
    const auto &lt = m_lastLifetimes.size() > i
                        ? m_lastLifetimes[i]
                        : std::pair<uint32_t, uint32_t>{0u, 0u};

    const char *firstPass = "n/a";
    const char *lastPass = "n/a";
    if (lt.first < m_lastOrder.size())
      firstPass = m_passes[m_lastOrder[lt.first]].name.c_str();
    if (lt.second < m_lastOrder.size())
      lastPass = m_passes[m_lastOrder[lt.second]].name.c_str();

    Log::Info("RG: {} {}x{} {} lifetime {} -> {}", name, desc.w, desc.h,
              fmtName(desc.fmt), firstPass, lastPass);
  }
}

} // namespace Nyx

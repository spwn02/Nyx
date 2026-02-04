#pragma once

#include "render/rg/RGDesc.h"
#include "render/rg/RGResource.h"
#include "render/rg/RGResources.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Nyx {

struct RenderPassContext;

enum class RenderAccess : uint32_t {
  None = 0,
  ColorWrite = 1u << 0,
  DepthWrite = 1u << 1,
  SampledRead = 1u << 2,
  ImageRead = 1u << 3,
  ImageWrite = 1u << 4,
  SSBORead = 1u << 5,
  SSBOWrite = 1u << 6,
  UBORead = 1u << 7,
};

inline RenderAccess operator|(RenderAccess a, RenderAccess b) {
  return (RenderAccess)((uint32_t)a | (uint32_t)b);
}
inline RenderAccess operator&(RenderAccess a, RenderAccess b) {
  return (RenderAccess)((uint32_t)a & (uint32_t)b);
}
inline RenderAccess &operator|=(RenderAccess &a, RenderAccess b) {
  a = a | b;
  return a;
}
inline bool hasAccess(RenderAccess v, RenderAccess mask) {
  return (((uint32_t)v) & (uint32_t)mask) != 0u;
}

enum class RenderExtentKind : uint8_t {
  Window,
  Viewport,
  Framebuffer,
  Explicit,
};

struct RenderExtent {
  RenderExtentKind kind = RenderExtentKind::Framebuffer;
  uint32_t w = 1;
  uint32_t h = 1;
};

struct RenderTextureDesc {
  RGFormat format = RGFormat::RGBA8;
  RGTexUsage usage = RGTexUsage::None;
  RenderExtent extent{};
  uint32_t layers = 1;
  uint32_t mipCount = 1;
};

struct RGTextureRef {
  uint32_t id = 0;
};

inline bool operator==(RGTextureRef a, RGTextureRef b) { return a.id == b.id; }
inline bool operator!=(RGTextureRef a, RGTextureRef b) { return a.id != b.id; }
static constexpr RGTextureRef InvalidRGTexture{0u};

struct RGBufferRef {
  uint32_t id = 0;
};

inline bool operator==(RGBufferRef a, RGBufferRef b) { return a.id == b.id; }
inline bool operator!=(RGBufferRef a, RGBufferRef b) { return a.id != b.id; }
static constexpr RGBufferRef InvalidRGBuffer{0u};

class RenderResourceBlackboard final {
public:
  void reset();

  RGTextureRef declareTexture(const std::string &name,
                              const RenderTextureDesc &desc);
  RGTextureRef getTexture(const std::string &name) const;

  const RenderTextureDesc &textureDesc(RGTextureRef ref) const;
  RGHandle textureHandle(RGTextureRef ref) const;
  void setTextureHandle(RGTextureRef ref, RGHandle handle);

  const std::string &textureName(RGTextureRef ref) const;
  uint32_t textureCount() const { return (uint32_t)m_textures.size(); }

  RGBufferRef declareBuffer(const std::string &name,
                            const RGBufferDesc &desc);
  RGBufferRef getBuffer(const std::string &name) const;

  const RGBufferDesc &bufferDesc(RGBufferRef ref) const;
  RGBufHandle bufferHandle(RGBufferRef ref) const;
  void setBufferHandle(RGBufferRef ref, RGBufHandle handle);

  const std::string &bufferName(RGBufferRef ref) const;
  uint32_t bufferCount() const { return (uint32_t)m_buffers.size(); }

  void bindExternalBuffer(RGBufferRef ref, const GLBuffer &buf);
  const GLBuffer &externalBuffer(RGBufferRef ref) const;
  bool isExternalBuffer(RGBufferRef ref) const;

private:
  struct TextureEntry {
    std::string name;
    RenderTextureDesc desc{};
    RGHandle handle = InvalidRG;
  };

  std::vector<TextureEntry> m_textures;
  std::unordered_map<std::string, uint32_t> m_texByName;

  struct BufferEntry {
    std::string name;
    RGBufferDesc desc{};
    RGBufHandle handle = InvalidRG;
    GLBuffer external{};
    bool externalBound = false;
  };

  std::vector<BufferEntry> m_buffers;
  std::unordered_map<std::string, uint32_t> m_bufByName;
};

class RenderPassBuilder final {
public:
  RenderPassBuilder(RenderResourceBlackboard &bb,
                    std::vector<std::pair<uint32_t, RenderAccess>> &texUses,
                    std::vector<std::pair<uint32_t, RenderAccess>> &bufUses)
      : m_bb(bb), m_texUses(texUses), m_bufUses(bufUses) {}

  RGTextureRef readTexture(const std::string &name,
                           RenderAccess access = RenderAccess::SampledRead);
  RGTextureRef writeTexture(const std::string &name,
                            RenderAccess access = RenderAccess::ColorWrite);
  RGTextureRef createTexture(const std::string &name,
                             const RenderTextureDesc &desc,
                             RenderAccess access = RenderAccess::ColorWrite);

  RGBufferRef readBuffer(const std::string &name,
                         RenderAccess access = RenderAccess::SSBORead);
  RGBufferRef writeBuffer(const std::string &name,
                          RenderAccess access = RenderAccess::SSBOWrite);

private:
  RenderResourceBlackboard &m_bb;
  std::vector<std::pair<uint32_t, RenderAccess>> &m_texUses;
  std::vector<std::pair<uint32_t, RenderAccess>> &m_bufUses;
};

class RenderGraph final {
public:
  using SetupFn =
      std::function<void(RenderPassBuilder &builder)>;
  using ExecuteFn = std::function<void(const RenderPassContext &ctx,
                                       RenderResourceBlackboard &bb,
                                       RGResources &rg)>;

  void reset();

  RGTextureRef declareTexture(const std::string &name,
                              const RenderTextureDesc &desc) {
    return m_blackboard.declareTexture(name, desc);
  }
  RGBufferRef declareBuffer(const std::string &name,
                            const RGBufferDesc &desc) {
    return m_blackboard.declareBuffer(name, desc);
  }
  RenderResourceBlackboard &blackboard() { return m_blackboard; }
  const RenderResourceBlackboard &blackboard() const { return m_blackboard; }

  void addPass(std::string name, SetupFn setup, ExecuteFn exec);

  void execute(const RenderPassContext &ctx, RGResources &rg);

  void enableDebug(const std::string &dotPath, bool dumpLifetimes);
  void enableValidation(bool enabled) { m_validate = enabled; }
  void dumpGraphDot() const;
  void dumpResourceLifetimes() const;

  // Legacy passthrough (kept until core passes migrate).
  void addPass(std::string name, std::function<void(RGResources &)> fn);
  void execute(RGResources &r);

private:
  struct PassNode {
    std::string name;
    SetupFn setup;
    ExecuteFn exec;
    std::vector<std::pair<uint32_t, RenderAccess>> texUses;
    std::vector<std::pair<uint32_t, RenderAccess>> bufUses;
    uint32_t order = 0;
  };

  struct LegacyPass {
    std::string name;
    std::function<void(RGResources &)> exec;
  };

  RenderResourceBlackboard m_blackboard;
  std::vector<PassNode> m_passes;
  std::vector<LegacyPass> m_legacy;

  struct AliasEntry {
    RGHandle handle = InvalidRG;
    RGTexDesc desc{};
  };
  std::vector<AliasEntry> m_aliasPool;

  bool m_debugEnabled = false;
  bool m_debugDumpLifetimes = false;
  std::string m_debugDotPath;
  bool m_validate = true;
  std::vector<uint32_t> m_lastOrder;
  std::vector<std::vector<uint32_t>> m_lastEdges;
  std::vector<std::pair<uint32_t, uint32_t>> m_lastLifetimes;
  std::vector<RGTexDesc> m_lastResolved;
};

} // namespace Nyx

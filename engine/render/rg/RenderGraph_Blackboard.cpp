#include "RenderGraph.h"

#include "core/Assert.h"

namespace Nyx {

void RenderResourceBlackboard::reset() {
  m_textures.clear();
  m_texByName.clear();
  m_buffers.clear();
  m_bufByName.clear();
}

RGTextureRef RenderResourceBlackboard::declareTexture(
    const std::string &name, const RenderTextureDesc &desc) {
  auto it = m_texByName.find(name);
  if (it != m_texByName.end()) {
    const uint32_t idx = it->second;
    NYX_ASSERT(m_textures[idx].desc.format == desc.format,
               "RenderGraph texture desc mismatch");
    NYX_ASSERT(m_textures[idx].desc.usage == desc.usage,
               "RenderGraph texture usage mismatch");
    NYX_ASSERT(m_textures[idx].desc.extent.kind == desc.extent.kind,
               "RenderGraph texture extent mismatch");
    NYX_ASSERT(m_textures[idx].desc.mipCount == desc.mipCount,
               "RenderGraph texture mip count mismatch");
    if (desc.extent.kind == RenderExtentKind::Explicit) {
      NYX_ASSERT(m_textures[idx].desc.extent.w == desc.extent.w,
                 "RenderGraph texture extent width mismatch");
      NYX_ASSERT(m_textures[idx].desc.extent.h == desc.extent.h,
                 "RenderGraph texture extent height mismatch");
    }
    return RGTextureRef{idx + 1};
  }

  const uint32_t idx = (uint32_t)m_textures.size();
  m_textures.push_back(TextureEntry{.name = name, .desc = desc});
  m_texByName.emplace(name, idx);
  return RGTextureRef{idx + 1};
}

RGTextureRef RenderResourceBlackboard::getTexture(const std::string &name) const {
  auto it = m_texByName.find(name);
  if (it == m_texByName.end())
    return InvalidRGTexture;
  return RGTextureRef{it->second + 1};
}

const RenderTextureDesc &
RenderResourceBlackboard::textureDesc(RGTextureRef ref) const {
  NYX_ASSERT(ref != InvalidRGTexture, "Invalid RGTextureRef");
  const uint32_t idx = ref.id - 1;
  NYX_ASSERT(idx < m_textures.size(), "Invalid RGTextureRef");
  return m_textures[idx].desc;
}

RGHandle RenderResourceBlackboard::textureHandle(RGTextureRef ref) const {
  NYX_ASSERT(ref != InvalidRGTexture, "Invalid RGTextureRef");
  const uint32_t idx = ref.id - 1;
  NYX_ASSERT(idx < m_textures.size(), "Invalid RGTextureRef");
  return m_textures[idx].handle;
}

void RenderResourceBlackboard::setTextureHandle(RGTextureRef ref,
                                                RGHandle handle) {
  NYX_ASSERT(ref != InvalidRGTexture, "Invalid RGTextureRef");
  const uint32_t idx = ref.id - 1;
  NYX_ASSERT(idx < m_textures.size(), "Invalid RGTextureRef");
  m_textures[idx].handle = handle;
}

const std::string &RenderResourceBlackboard::textureName(RGTextureRef ref) const {
  NYX_ASSERT(ref != InvalidRGTexture, "Invalid RGTextureRef");
  const uint32_t idx = ref.id - 1;
  NYX_ASSERT(idx < m_textures.size(), "Invalid RGTextureRef");
  return m_textures[idx].name;
}

RGBufferRef RenderResourceBlackboard::declareBuffer(
    const std::string &name, const RGBufferDesc &desc) {
  auto it = m_bufByName.find(name);
  if (it != m_bufByName.end()) {
    const uint32_t idx = it->second;
    NYX_ASSERT(m_buffers[idx].desc == desc,
               "RenderGraph buffer desc mismatch");
    return RGBufferRef{idx + 1};
  }

  const uint32_t idx = (uint32_t)m_buffers.size();
  m_buffers.push_back(BufferEntry{.name = name, .desc = desc});
  m_bufByName.emplace(name, idx);
  return RGBufferRef{idx + 1};
}

RGBufferRef RenderResourceBlackboard::getBuffer(const std::string &name) const {
  auto it = m_bufByName.find(name);
  if (it == m_bufByName.end())
    return InvalidRGBuffer;
  return RGBufferRef{it->second + 1};
}

const RGBufferDesc &
RenderResourceBlackboard::bufferDesc(RGBufferRef ref) const {
  NYX_ASSERT(ref != InvalidRGBuffer, "Invalid RGBufferRef");
  const uint32_t idx = ref.id - 1;
  NYX_ASSERT(idx < m_buffers.size(), "Invalid RGBufferRef");
  return m_buffers[idx].desc;
}

RGBufHandle RenderResourceBlackboard::bufferHandle(RGBufferRef ref) const {
  NYX_ASSERT(ref != InvalidRGBuffer, "Invalid RGBufferRef");
  const uint32_t idx = ref.id - 1;
  NYX_ASSERT(idx < m_buffers.size(), "Invalid RGBufferRef");
  return m_buffers[idx].handle;
}

void RenderResourceBlackboard::setBufferHandle(RGBufferRef ref,
                                               RGBufHandle handle) {
  NYX_ASSERT(ref != InvalidRGBuffer, "Invalid RGBufferRef");
  const uint32_t idx = ref.id - 1;
  NYX_ASSERT(idx < m_buffers.size(), "Invalid RGBufferRef");
  m_buffers[idx].handle = handle;
}

const std::string &RenderResourceBlackboard::bufferName(RGBufferRef ref) const {
  NYX_ASSERT(ref != InvalidRGBuffer, "Invalid RGBufferRef");
  const uint32_t idx = ref.id - 1;
  NYX_ASSERT(idx < m_buffers.size(), "Invalid RGBufferRef");
  return m_buffers[idx].name;
}

void RenderResourceBlackboard::bindExternalBuffer(RGBufferRef ref,
                                                  const GLBuffer &buf) {
  NYX_ASSERT(ref != InvalidRGBuffer, "Invalid RGBufferRef");
  const uint32_t idx = ref.id - 1;
  NYX_ASSERT(idx < m_buffers.size(), "Invalid RGBufferRef");
  m_buffers[idx].external = buf;
  m_buffers[idx].externalBound = true;
}

const GLBuffer &
RenderResourceBlackboard::externalBuffer(RGBufferRef ref) const {
  static GLBuffer dummy{};
  NYX_ASSERT(ref != InvalidRGBuffer, "Invalid RGBufferRef");
  const uint32_t idx = ref.id - 1;
  NYX_ASSERT(idx < m_buffers.size(), "Invalid RGBufferRef");
  const auto &b = m_buffers[idx];
  return b.externalBound ? b.external : dummy;
}

bool RenderResourceBlackboard::isExternalBuffer(RGBufferRef ref) const {
  NYX_ASSERT(ref != InvalidRGBuffer, "Invalid RGBufferRef");
  const uint32_t idx = ref.id - 1;
  NYX_ASSERT(idx < m_buffers.size(), "Invalid RGBufferRef");
  return m_buffers[idx].externalBound;
}

RGTextureRef RenderPassBuilder::readTexture(const std::string &name,
                                            RenderAccess access) {
  RGTextureRef ref = m_bb.getTexture(name);
  NYX_ASSERT(ref != InvalidRGTexture, "RenderGraph missing texture");
  const uint32_t res = ref.id - 1;
  for (auto &u : m_texUses) {
    if (u.first == res) {
      u.second |= access;
      return ref;
    }
  }
  m_texUses.emplace_back(res, access);
  return ref;
}

RGTextureRef RenderPassBuilder::writeTexture(const std::string &name,
                                             RenderAccess access) {
  RGTextureRef ref = m_bb.getTexture(name);
  NYX_ASSERT(ref != InvalidRGTexture, "RenderGraph missing texture");
  const uint32_t res = ref.id - 1;
  for (auto &u : m_texUses) {
    if (u.first == res) {
      u.second |= access;
      return ref;
    }
  }
  m_texUses.emplace_back(res, access);
  return ref;
}

RGTextureRef RenderPassBuilder::createTexture(const std::string &name,
                                              const RenderTextureDesc &desc,
                                              RenderAccess access) {
  RGTextureRef ref = m_bb.declareTexture(name, desc);
  const uint32_t res = ref.id - 1;
  for (auto &u : m_texUses) {
    if (u.first == res) {
      u.second |= access;
      return ref;
    }
  }
  m_texUses.emplace_back(res, access);
  return ref;
}

RGBufferRef RenderPassBuilder::readBuffer(const std::string &name,
                                          RenderAccess access) {
  RGBufferRef ref = m_bb.getBuffer(name);
  NYX_ASSERT(ref != InvalidRGBuffer, "RenderGraph missing buffer");
  const uint32_t res = ref.id - 1;
  for (auto &u : m_bufUses) {
    if (u.first == res) {
      u.second |= access;
      return ref;
    }
  }
  m_bufUses.emplace_back(res, access);
  return ref;
}

RGBufferRef RenderPassBuilder::writeBuffer(const std::string &name,
                                           RenderAccess access) {
  RGBufferRef ref = m_bb.getBuffer(name);
  NYX_ASSERT(ref != InvalidRGBuffer, "RenderGraph missing buffer");
  const uint32_t res = ref.id - 1;
  for (auto &u : m_bufUses) {
    if (u.first == res) {
      u.second |= access;
      return ref;
    }
  }
  m_bufUses.emplace_back(res, access);
  return ref;
}

} // namespace Nyx

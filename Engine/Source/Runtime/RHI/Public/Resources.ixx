export module Nyx.RHI:Resources;

import std;
import Miracle;

import :Forward;
import :Types;
import :ResourcePool;
import :Handle;

using namespace Miracle;

export namespace Nyx::RHI {

struct TextureTag {};
using TextureHandle = Handle<TextureTag>;

struct TextureViewTag {};
using TextureViewHandle = Handle<TextureViewTag>;

struct TextureViewDescriptor {
  String label;
  TextureHandle texture;
  TextureAspect aspects{TextureAspect::Color};
  u32 baseMipLevel{};
  u32 mipLevelCount{};
  u32 baseArrayLayer{};
  u32 arrayLayerCount{};
};

/// Texture is the stable logical record exposed by the RHI. The Vulkan image, allocation, and dispatch state
/// are stored beside it in Device's private resource pool. This keeps the public type free of Vulkan headers
/// and avoids pimpl while still allowing the backend resource to be owned by Device.
class Texture final {
public:
  ~Texture() noexcept = default;

  Texture(const Texture &) = delete ("Texture records are owned by Device and cannot be copied.");
  auto operator=(const Texture &)
      -> Texture & = delete ("Texture records are owned by Device and cannot be copied.");

  Texture(Texture &&) noexcept = default;
  auto operator=(Texture &&) noexcept -> Texture & = default;

  [[nodiscard]]
  auto handle() const noexcept -> TextureHandle {
    return handle_;
  }

  [[nodiscard]]
  auto descriptor() const -> TextureDescriptor {
    return descriptor_;
  }

private:
  friend class Device;
  template <HandleTag, class>
  friend class detail::ResourcePool;

  Texture(TextureHandle handle, TextureDescriptor descriptor) noexcept
      : handle_(handle)
      , descriptor_(std::move(descriptor)) {
  }

  TextureHandle handle_;
  TextureDescriptor descriptor_;
  u32 viewCount_{};
};

class TextureView final {
public:
  ~TextureView() noexcept = default;

  TextureView(const TextureView &) = delete (
      "Texture view records are owned by Device and cannot be copied.");
  auto operator=(const TextureView &)
      -> TextureView & = delete ("Texture view records are owned by Device and cannot be copied.");

  TextureView(TextureView &&) noexcept = default;
  auto operator=(TextureView &&) noexcept -> TextureView & = default;

  [[nodiscard]]
  auto handle() const noexcept -> TextureViewHandle {
    return handle_;
  }

  [[nodiscard]]
  auto descriptor() const noexcept -> const TextureViewDescriptor & {
    return descriptor_;
  }

private:
  friend class Device;
  template <HandleTag, class>
  friend class detail::ResourcePool;

  TextureView(TextureViewHandle handle, TextureViewDescriptor descriptor) noexcept
      : handle_(handle)
      , descriptor_(std::move(descriptor)) {
  }

  TextureViewHandle handle_;
  TextureViewDescriptor descriptor_;
};

struct PipelineTag {};
using PipelineHandle = Handle<PipelineTag>;

struct[[= debug::derive]] PipelineDescriptor {
  String label;
  PipelineBindPoint bindPoint{PipelineBindPoint::Graphics};
};

class Pipeline final {
public:
  ~Pipeline() noexcept = default;

  Pipeline(const Pipeline &) = delete ("Pipeline records are owned by Device and cannot be copied.");
  auto operator=(const Pipeline &)
      -> Pipeline & = delete ("Pipeline records are owned by Device and cannot be copied.");

  Pipeline(Pipeline &&) noexcept = default;
  auto operator=(Pipeline &&) noexcept -> Pipeline & = default;

  [[nodiscard]]
  auto handle() const noexcept -> PipelineHandle {
    return handle_;
  }

  [[nodiscard]]
  auto descriptor() const noexcept -> const PipelineDescriptor & {
    return descriptor_;
  }

private:
  friend class Device;
  template <HandleTag, class>
  friend class detail::ResourcePool;

  Pipeline(PipelineHandle handle, PipelineDescriptor descriptor) noexcept
      : handle_(handle)
      , descriptor_(std::move(descriptor)) {
  }

  PipelineHandle handle_;
  PipelineDescriptor descriptor_;
};

}

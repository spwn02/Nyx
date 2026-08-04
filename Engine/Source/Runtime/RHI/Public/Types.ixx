export module Nyx.RHI:Types;

import std;
import Nyx.Core;
import :Forward;

export namespace Nyx::RHI {

struct[[= debug::derive]] ApiVersion {
  u32 major{1}, minor{}, patch{};

  friend constexpr auto operator<=>(const ApiVersion &, const ApiVersion &) noexcept
      -> std::strong_ordering = default;
};

struct[[= debug::derive]] Extent2D {
  u32 width{1}, height{1};
};

struct[[= debug::derive]] Offset2D {
  i32 x{}, y{};
};

struct[[= debug::derive]] Rect2D {
  Offset2D offset{};
  Extent2D extent{};
};

struct[[= debug::derive]] Extent3D {
  u32 width{1}, height{1}, depth{1};
};

enum class[[= debug::derive]] BackendType : u8 {
  Vulkan,
};

enum class[[= debug::derive]] SurfaceColorSpace : u8 {
  SrgbNonlinear,
};

enum class[[= debug::derive]] AdapterType : u8 {
  Other,
  Integrated,
  Discrete,
  Virtual,
  Cpu,
};

enum class[[= bitflags]] DebugMessageSeverity : u8 {
  Verbose = 1U << 0,
  Info = 1U << 1,
  Warning = 1U << 2,
  Error = 1U << 3,
};

enum class[[= debug::derive]] PresentMode : u8 {
  Fifo,
  Mailbox,
  Immediate,
  FifoRelaxed,
};

enum class[[= debug::derive]] TextureDimension : u8 {
  D1,
  D2,
  D3,
};

enum class[[= debug::derive]] TextureComponentType : u8 {
  Undefined,
  Unorm,
  Snorm,
  Uint,
  Sint,
  Float,
  Depth,
  DepthStencil,
};

enum class[[= bitflags]] TextureAspect : u8 {
  Color = 1U << 0,
  Depth = 1U << 1,
  Stencil = 1U << 2,
};

using TextureAspects = TextureAspect;

enum class[[= bitflags]] TextureUsage : u8 {
  TransferSrc = 1U << 0,
  TransferDst = 1U << 1,
  Sampled = 1U << 2,
  Storage = 1U << 3,
  ColorAttachment = 1U << 4,
  DepthStencilAttachment = 1U << 5,
  InputAttachment = 1U << 6,
  Transient = 1U << 7,
};

enum class[[= debug::derive]] TextureLayout : u8 {
  Undefined,
  General,
  ColorAttachment,
  DepthStencilAttachment,
  DepthStencilReadOnly,
  ShaderReadOnly,
  TransferSrc,
  TransferDst,
  Present,
};

enum class[[= debug::derive]] LoadOp : u8 {
  Load,
  Clear,
  DontCare,
};

enum class[[= debug::derive]] StoreOp : u8 {
  Store,
  DontCare,
};

enum class[[= debug::derive]] PipelineBindPoint : u8 {
  Graphics,
  Compute,
};

enum class[[= bitflags]] PipelineStage : u16 {
  None = 0,
  TopOfPipe = 1 << 0,
  DrawIndirect = 1 << 1,
  VertexInput = 1 << 2,
  VertexShader = 1 << 3,
  FragmentShader = 1 << 4,
  EarlyFragmentTests = 1 << 5,
  LateFragmentTests = 1 << 6,
  ColorAttachmentOutput = 1 << 7,
  ComputeShader = 1 << 8,
  Transfer = 1 << 9,
  BottomOfPipe = 1 << 10,
  Host = 1 << 11,
  AllGraphics = 1 << 12,
  AllCommands = 1 << 13,
};

enum class[[= bitflags]] Access : u32 {
  None = 0,
  IndirectCommandRead = 1 << 0,
  IndexRead = 1 << 1,
  VertexAttributeRead = 1 << 2,
  UniformRead = 1 << 3,
  InputAttachmentRead = 1 << 4,
  ShaderRead = 1 << 5,
  ShaderWrite = 1 << 6,
  ColorAttachmentRead = 1 << 7,
  ColorAttachmentWrite = 1 << 8,
  DepthStencilAttachmentRead = 1 << 9,
  DepthStenchilAttachmentWrite = 1 << 10,
  TransferRead = 1 << 11,
  TransferWrite = 1 << 12,
  HostRead = 1 << 13,
  HostWrite = 1 << 14,
  MemoryRead = 1 << 15,
  MemoryWrite = 1 << 16,
};

enum class[[= debug::derive]] ShaderStage : u8 {
  Vertex,
  Fragment,
  Compute,
};

struct[[= debug::derive]] ShaderStageDescriptor {
  ShaderStage stage;
  Vec<u32> code;
  String entryPoint;
};

struct[[= debug::derive]] TextureSubresourceRange {
  TextureAspect aspects{TextureAspect::Color};
  u32 baseMipLevel{};
  u32 levelCount{};
  u32 baseArrayLevel{};
  u32 layerCount{};
};

enum class[[= debug::derive]] TextureSampleCount : u8 {
  X1 = 1,
  X2 = 2,
  X4 = 4,
  X8 = 8,
  X16 = 16,
  X32 = 32,
  X64 = 64,
};

enum class[[= debug::derive]] QueueRole : u8 {
  Graphics,
  Compute,
  Transfer,
  Present,
};

enum class[[= bitflags]] QueueCapabilities : u8 {
  Graphics = 1U << 0,
  Compute = 1U << 1,
  Transfer = 1U << 2,
  SparseBinding = 1U << 3,
};

enum class[[= bitflags]] DeviceFeature : u8 {
  TimelineSemaphore = 1U << 0,
  Synchronization2 = 1U << 1,
  DynamicRendering = 1U << 2,
  DescriptorIndexing = 1U << 3,
  BufferDeviceAddress = 1U << 4,
  SamplerAnisotropy = 1U << 5,
};

using DeviceFeatures = DeviceFeature;

enum class[[= bitflags]] TextureFormatFeatures : u8 {
  Sampled = 1U << 0,
  Storage = 1U << 1,
  ColorAttachment = 1U << 2,
  DepthStencilAttachment = 1U << 3,
  BlitSrc = 1U << 4,
  BlitDst = 1U << 5,
  LinearFilter = 1U << 6,
};

struct QueueRequest {
  QueueRole role{QueueRole::Graphics};
  QueueCapabilities required{};
  u32 count{1};
  f32 priority{1.0F};
  Option<Ref<const Surface>> presentSurface{None};
};

struct DeviceDescriptor {
  String label;
  Span<const QueueRequest> queues;
  Span<const StringView> requiredExtensions;
  Span<const StringView> optionalExtensions;
  DeviceFeatures requiredFeatures{DeviceFeature::TimelineSemaphore | DeviceFeature::Synchronization2 |
                                  DeviceFeature::DynamicRendering | DeviceFeature::BufferDeviceAddress};
  DeviceFeatures optionalFeatures{};
};

struct TextureFormatAnnotation {
  TextureAspect aspects{};
  TextureComponentType componentType{TextureComponentType::Undefined};
  u8 componentCount{};
  u8 bytesPerComponent{};
  u8 blockWidth{1};
  u8 blockHeight{1};
  u8 blockBytes{};
  bool srgb{};
};

namespace format {

template <u8 Components, u8 BytesPerComponent>
consteval auto unorm() -> TextureFormatAnnotation {
  return {
      .aspects = TextureAspect::Color,
      .componentType = TextureComponentType::Unorm,
      .componentCount = Components,
      .bytesPerComponent = BytesPerComponent,
      .blockBytes = static_cast<u8>(Components * BytesPerComponent),
  };
}

template <u8 Components, u8 BytesPerComponent>
consteval auto snorm() -> TextureFormatAnnotation {
  auto result = unorm<Components, BytesPerComponent>();
  result.componentType = TextureComponentType::Snorm;
  return result;
}

template <u8 Components, u8 BytesPerComponent>
consteval auto uint() -> TextureFormatAnnotation {
  auto result = unorm<Components, BytesPerComponent>();
  result.componentType = TextureComponentType::Uint;
  return result;
}

template <u8 Components, u8 BytesPerComponent>
consteval auto sint() -> TextureFormatAnnotation {
  auto result = unorm<Components, BytesPerComponent>();
  result.componentType = TextureComponentType::Sint;
  return result;
}

template <u8 Components, u8 BytesPerComponent>
consteval auto floating() -> TextureFormatAnnotation {
  auto result = unorm<Components, BytesPerComponent>();
  result.componentType = TextureComponentType::Float;
  return result;
}

template <u8 Components, u8 BytesPerComponent>
consteval auto srgb() -> TextureFormatAnnotation {
  auto result = unorm<Components, BytesPerComponent>();
  result.srgb = true;
  return result;
}

template <u8 Bytes>
consteval auto depth() -> TextureFormatAnnotation {
  return {
      .aspects = TextureAspect::Depth,
      .componentType = TextureComponentType::Depth,
      .componentCount = 1,
      .bytesPerComponent = Bytes,
      .blockBytes = Bytes,
  };
}

template <u8 Bytes>
consteval auto depthStencil() -> TextureFormatAnnotation {
  return {
      .aspects = TextureAspect::Depth | TextureAspect::Stencil,
      .componentType = TextureComponentType::DepthStencil,
      .componentCount = 2,
      .bytesPerComponent = Bytes,
      .blockBytes = Bytes,
  };
}

template <u8 BlockWidth, u8 BlockHeight, u8 BlockBytes, bool Srgb = false>
consteval auto compressed() -> TextureFormatAnnotation {
  return {
      .aspects = TextureAspect::Color,
      .componentType = TextureComponentType::Unorm,
      .componentCount = 4,
      .blockWidth = BlockWidth,
      .blockHeight = BlockHeight,
      .blockBytes = BlockBytes,
      .srgb = Srgb,
  };
}

} // namespace format

enum class[[= debug::derive]] TextureFormat : u8 {
  Undefined,

  R8Unorm[[= format::unorm<1, 1>()]],
  R8Snorm[[= format::snorm<1, 1>()]],
  R8Uint[[= format::uint<1, 1>()]],
  R8Sint[[= format::sint<1, 1>()]],

  Rg8Unorm[[= format::unorm<2, 1>()]],
  Rg8Snorm[[= format::snorm<2, 1>()]],
  Rg8Uint[[= format::uint<2, 1>()]],
  Rg8Sint[[= format::sint<2, 1>()]],

  Rgba8Unorm[[= format::unorm<4, 1>()]],
  Rgba8UnormSrgb[[= format::srgb<4, 1>()]],
  Rgba8Uint[[= format::uint<4, 1>()]],
  Rgba8Sint[[= format::sint<4, 1>()]],

  Bgra8Unorm[[= format::unorm<4, 1>()]],
  Bgra8UnormSrgb[[= format::srgb<4, 1>()]],

  R16Float[[= format::floating<1, 2>()]],
  Rg16Float[[= format::floating<2, 2>()]],
  Rgba16Float[[= format::floating<4, 2>()]],

  R32Float[[= format::floating<1, 4>()]],
  Rg32Float[[= format::floating<2, 4>()]],
  Rgba32Float[[= format::floating<4, 4>()]],

  D16Unorm[[= format::depth<2>()]],
  D24UnormS8Uint[[= format::depthStencil<4>()]],
  D32Float[[= format::depth<4>()]],
  D32FloatS8Uint[[= format::depthStencil<8>()]],

  Bc1RgbaUnorm[[= format::compressed<4, 4, 8>()]],
  Bc1RgbaUnormSrgb[[= format::compressed<4, 4, 8, true>()]],
  Bc3Unorm[[= format::compressed<4, 4, 16>()]],
  Bc3UnormSrgb[[= format::compressed<4, 4, 16, true>()]],
  Bc5Unorm[[= format::compressed<4, 4, 16>()]],
  Bc7Unorm[[= format::compressed<4, 4, 16>()]],
  Bc7UnormSrgb[[= format::compressed<4, 4, 16, true>()]],

  Count,
};

struct[[= debug::derive]] SurfaceFormat {
  TextureFormat format{TextureFormat::Undefined};
  SurfaceColorSpace colorSpace{SurfaceColorSpace::SrgbNonlinear};
};

struct[[= debug::derive]] TextureFormatInfo {
  TextureFormat format{TextureFormat::Undefined};
  StringView name;
  TextureAspect aspects{};
  TextureComponentType componentType{TextureComponentType::Undefined};
  u8 componentCount{};
  u8 bytesPerComponent{};
  u8 blockWidth{1};
  u8 blockHeight{1};
  u8 blockBytes{};
  bool srgb{};
};

/// TODO: Remove this line once stable Clang C++26 reflection support comes out
// NOLINTBEGIN(bugprone-reserved-identifier, readability-identifier-naming)
namespace detail {

template <TextureFormat Format>
constexpr auto makeTextureFormatInfo() -> TextureFormatInfo {
  TextureFormatInfo result{.format = Format};
  bool found{};

  template for (constexpr std::meta::info enumerator : meta::enumerators<^^TextureFormat>) {
    if constexpr ([:enumerator:] == Format) {
      result.name = meta::identifier<enumerator>;

      template for (constexpr std::meta::info annotation : meta::annotations<enumerator>) {
        if constexpr (std::meta::type_of(annotation) == ^^TextureFormatAnnotation) {
          const auto properties = std::meta::extract<TextureFormatAnnotation>(annotation);

          result = {
              .format = Format,
              .aspects = properties.aspects,
              .componentType = properties.componentType,
              .componentCount = properties.componentCount,
              .bytesPerComponent = properties.bytesPerComponent,
              .blockWidth = properties.blockWidth,
              .blockHeight = properties.blockHeight,
              .blockBytes = properties.blockBytes,
              .srgb = properties.srgb,
          };
          found = true;
        }
      }
    }
  }

  if constexpr (Format != TextureFormat::Undefined and Format != TextureFormat::Count)
    if (!found)
      throw std::logic_error("Every non-undefined TextureFormat must have format properties.");

  return result;
}

} // namespace detail

// NOLINTBEGIN(readability-identifier-naming)
template <TextureFormat Format>
inline constexpr auto texture_format_info_v = detail::makeTextureFormatInfo<Format>();
// NOLINTEND(readability-identifier-naming)

inline constexpr auto textureFormatMetadata =
    [] consteval -> Array<TextureFormatInfo, static_cast<usize>(TextureFormat::Count)> {
  Array<TextureFormatInfo, static_cast<usize>(TextureFormat::Count)> result{};

  template for (constexpr std::meta::info enumerator : meta::enumerators<^^TextureFormat>) {
    if constexpr ([:enumerator:] != TextureFormat::Count)
      result[static_cast<usize>([:enumerator:])] = detail::makeTextureFormatInfo<([:enumerator:])>();
  }

  return result;
}
();
/// TODO: Remove this line once stable Clang C++26 reflection support comes out
// NOLINTEND(bugprone-reserved-identifier, readability-identifier-naming)

constexpr auto textureFormatInfo(TextureFormat format) noexcept -> TextureFormatInfo {
  const auto index = static_cast<usize>(format);

  if (index >= textureFormatMetadata.size())
    return {};

  return textureFormatMetadata.at(index);
}

constexpr auto textureLevelByteSize(const TextureFormatInfo &format, Extent3D extent) noexcept -> u64 {
  const u32 blockWidth = static_cast<u32>(format.blockWidth);
  const u32 blockHeight = static_cast<u32>(format.blockHeight);

  const u32 width = (extent.width + blockWidth - 1) / blockWidth;
  const u32 height = (extent.height + blockHeight - 1) / blockHeight;

  return static_cast<u64>(width) * height * extent.depth * format.blockBytes;
}

constexpr auto textureByteSize(TextureFormat format,
    Extent3D extent,
    u32 mipLevels = 1,
    u32 arrayLayers = 1) noexcept -> u64 {
  const TextureFormatInfo properties = textureFormatInfo(format);
  u64 size{};

  for (u32 mip{}; mip < mipLevels; ++mip) {
    const u32 width = extent.width >> mip;
    const u32 height = extent.height >> mip;
    const u32 depth = extent.depth >> mip;

    const Extent3D mipExtent{
        .width = width == 0 ? 1 : width,
        .height = height == 0 ? 1 : height,
        .depth = depth == 0 ? 1 : depth,
    };

    size += textureLevelByteSize(properties, mipExtent);
  }

  return size * arrayLayers;
}

struct[[= debug::derive]] TextureDescriptor {
  String label;
  TextureDimension dimension{TextureDimension::D2};
  Extent3D extent{};
  u32 mipLevels{1};
  u32 arrayLayers{1};
  TextureFormat format{TextureFormat::Undefined};
  TextureUsage usage{};
  TextureSampleCount sampleCount{TextureSampleCount::X1};
  TextureLayout initialLayout{TextureLayout::Undefined};
};

struct[[= debug::derive]] GraphicsPipelineDescriptor {
  String label;
  Vec<ShaderStageDescriptor> stages;
  Vec<TextureFormat> colorFormats;
  TextureFormat depthFormat;
  TextureFormat stencilFormat;
};

struct[[= debug::derive]] ComputePipelineDescriptor {
  String label;
  ShaderStageDescriptor stage;
};

namespace detail {

constexpr auto queueRoleCapabilities(QueueRole role) noexcept -> QueueCapabilities {
  switch (role) {
    case QueueRole::Graphics: return QueueCapabilities::Graphics;
    case QueueRole::Compute: return QueueCapabilities::Compute;
    case QueueRole::Transfer: return QueueCapabilities::Transfer;
    case QueueRole::Present: return {};
  }

  return {};
}

constexpr auto validateTextureDescriptor(const TextureDescriptor &desc) noexcept -> Result<void> {
  if (desc.format == TextureFormat::Undefined or desc.format == TextureFormat::Count)
    return bail({"Texture format must be specified."});

  if (desc.extent.width == 0 or desc.extent.height == 0 or desc.extent.depth == 0)
    return bail({"Texture extent must be non-zero."});

  if (desc.mipLevels == 0)
    return bail({"Texture must contain at least one mip level."});

  if (desc.arrayLayers == 0)
    return bail({"Texture must contain at least one array layer."});

  if (desc.usage == TextureUsage{})
    return bail({"Texture usage must not be empty."});

  if (desc.dimension == TextureDimension::D1 and (desc.extent.height != 1 or desc.extent.depth != 1))
    return bail({"One-dimensional textures must have height and depth equal to one."});

  if (desc.dimension == TextureDimension::D2 and desc.extent.depth != 1)
    return bail({"Two-dimensional textures must have depth equal to one."});

  if (desc.dimension == TextureDimension::D3 and desc.arrayLayers != 1)
    return bail({"Three-dimensional textures cannot have array layers."});

  return {};
}

} // namespace detail

} // namespace Nyx::RHI

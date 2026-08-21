module;

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

export module Nyx.RHI:Vulkan;

import Miracle;

import :Types;

using namespace Miracle;

export namespace Nyx::RHI::detail {

constexpr auto fromVkApiVersion(u32 version) noexcept -> ApiVersion {
  return {
      .major = VK_API_VERSION_MAJOR(version),
      .minor = VK_API_VERSION_MINOR(version),
      .patch = VK_API_VERSION_PATCH(version),
  };
}
constexpr auto toVkFormat(TextureFormat format) noexcept -> Option<VkFormat> {
  switch (format) {
    case TextureFormat::R8Unorm: return VK_FORMAT_R8_UNORM;
    case TextureFormat::R8Snorm: return VK_FORMAT_R8_SNORM;
    case TextureFormat::R8Uint: return VK_FORMAT_R8_UINT;
    case TextureFormat::R8Sint: return VK_FORMAT_R8_SINT;
    case TextureFormat::Rg8Unorm: return VK_FORMAT_R8G8_UNORM;
    case TextureFormat::Rg8Snorm: return VK_FORMAT_R8G8_SNORM;
    case TextureFormat::Rg8Uint: return VK_FORMAT_R8G8_UINT;
    case TextureFormat::Rg8Sint: return VK_FORMAT_R8G8_SINT;
    case TextureFormat::Rgba8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::Rgba8UnormSrgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case TextureFormat::Rgba8Uint: return VK_FORMAT_R8G8B8A8_UINT;
    case TextureFormat::Rgba8Sint: return VK_FORMAT_R8G8B8A8_SINT;
    case TextureFormat::Bgra8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::Bgra8UnormSrgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case TextureFormat::R16Float: return VK_FORMAT_R16_SFLOAT;
    case TextureFormat::Rg16Float: return VK_FORMAT_R16G16_SFLOAT;
    case TextureFormat::Rgba16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::R32Float: return VK_FORMAT_R32_SFLOAT;
    case TextureFormat::Rg32Float: return VK_FORMAT_R32G32_SFLOAT;
    case TextureFormat::Rgba32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case TextureFormat::D16Unorm: return VK_FORMAT_D16_UNORM;
    case TextureFormat::D24UnormS8Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
    case TextureFormat::D32Float: return VK_FORMAT_D32_SFLOAT;
    case TextureFormat::D32FloatS8Uint: return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case TextureFormat::Bc1RgbaUnorm: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case TextureFormat::Bc1RgbaUnormSrgb: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case TextureFormat::Bc3Unorm: return VK_FORMAT_BC3_UNORM_BLOCK;
    case TextureFormat::Bc3UnormSrgb: return VK_FORMAT_BC3_SRGB_BLOCK;
    case TextureFormat::Bc5Unorm: return VK_FORMAT_BC5_UNORM_BLOCK;
    case TextureFormat::Bc7Unorm: return VK_FORMAT_BC7_UNORM_BLOCK;
    case TextureFormat::Bc7UnormSrgb: return VK_FORMAT_BC7_SRGB_BLOCK;
    case TextureFormat::Undefined:
    case TextureFormat::Count: return None;
  }

  return None;
}

constexpr auto toVkImageType(TextureDimension dimension) noexcept -> VkImageType {
  switch (dimension) {
    case TextureDimension::D1: return VK_IMAGE_TYPE_1D;
    case TextureDimension::D2: return VK_IMAGE_TYPE_2D;
    case TextureDimension::D3: return VK_IMAGE_TYPE_3D;
  }

  return VK_IMAGE_TYPE_2D;
}

constexpr auto toVkSampleCount(TextureSampleCount sampleCount) noexcept -> VkSampleCountFlagBits {
  switch (sampleCount) {
    case TextureSampleCount::X1: return VK_SAMPLE_COUNT_1_BIT;
    case TextureSampleCount::X2: return VK_SAMPLE_COUNT_2_BIT;
    case TextureSampleCount::X4: return VK_SAMPLE_COUNT_4_BIT;
    case TextureSampleCount::X8: return VK_SAMPLE_COUNT_8_BIT;
    case TextureSampleCount::X16: return VK_SAMPLE_COUNT_16_BIT;
    case TextureSampleCount::X32: return VK_SAMPLE_COUNT_32_BIT;
    case TextureSampleCount::X64: return VK_SAMPLE_COUNT_64_BIT;
  }

  return VK_SAMPLE_COUNT_1_BIT;
}

constexpr auto toVkImageUsage(TextureUsage usage) noexcept -> VkImageUsageFlags {
  VkImageUsageFlags result{};

  if (has(usage, TextureUsage::TransferSrc))
    result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

  if (has(usage, TextureUsage::TransferDst))
    result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  if (has(usage, TextureUsage::Sampled))
    result |= VK_IMAGE_USAGE_SAMPLED_BIT;

  if (has(usage, TextureUsage::Storage))
    result |= VK_IMAGE_USAGE_STORAGE_BIT;

  if (has(usage, TextureUsage::ColorAttachment))
    result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  if (has(usage, TextureUsage::DepthStencilAttachment))
    result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

  if (has(usage, TextureUsage::InputAttachment))
    result |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

  if (has(usage, TextureUsage::Transient))
    result |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

  return result;
}

constexpr auto toVkImageLayout(TextureLayout layout) noexcept -> VkImageLayout {
  switch (layout) {
    case TextureLayout::Undefined: return VK_IMAGE_LAYOUT_UNDEFINED;
    case TextureLayout::General: return VK_IMAGE_LAYOUT_GENERAL;
    case TextureLayout::ColorAttachment: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case TextureLayout::DepthStencilAttachment: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case TextureLayout::DepthStencilReadOnly: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case TextureLayout::ShaderReadOnly: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case TextureLayout::TransferSrc: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case TextureLayout::TransferDst: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case TextureLayout::Present: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  }

  return VK_IMAGE_LAYOUT_UNDEFINED;
}

constexpr auto toVkLoadOp(LoadOp operation) noexcept -> VkAttachmentLoadOp {
  switch (operation) {
    case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  }

  return VK_ATTACHMENT_LOAD_OP_NONE;
}

constexpr auto toVkStoreOp(StoreOp operation) noexcept -> VkAttachmentStoreOp {
  switch (operation) {
    case StoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
    case StoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
  }

  return VK_ATTACHMENT_STORE_OP_NONE;
}

constexpr auto toVkImageAspect(TextureAspect aspect) noexcept -> VkImageAspectFlags {
  VkImageAspectFlags result{};

  if (has(aspect, TextureAspect::Color))
    result |= VK_IMAGE_ASPECT_COLOR_BIT;

  if (has(aspect, TextureAspect::Depth))
    result |= VK_IMAGE_ASPECT_DEPTH_BIT;

  if (has(aspect, TextureAspect::Stencil))
    result |= VK_IMAGE_ASPECT_STENCIL_BIT;

  return result;
}

constexpr auto toVkAccess(Access access) noexcept -> VkAccessFlags2 {
  VkAccessFlags2 result{};

  if (has(access, Access::IndirectCommandRead))
    result |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;

  if (has(access, Access::IndexRead))
    result |= VK_ACCESS_2_INDEX_READ_BIT;

  if (has(access, Access::VertexAttributeRead))
    result |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;

  if (has(access, Access::UniformRead))
    result |= VK_ACCESS_2_UNIFORM_READ_BIT;

  if (has(access, Access::InputAttachmentRead))
    result |= VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT;

  if (has(access, Access::ShaderRead))
    result |= VK_ACCESS_2_SHADER_READ_BIT;

  if (has(access, Access::ShaderWrite))
    result |= VK_ACCESS_2_SHADER_WRITE_BIT;

  if (has(access, Access::ColorAttachmentRead))
    result |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;

  if (has(access, Access::ColorAttachmentWrite))
    result |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

  if (has(access, Access::DepthStencilAttachmentRead))
    result |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

  if (has(access, Access::DepthStenchilAttachmentWrite))
    result |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  if (has(access, Access::TransferRead))
    result |= VK_ACCESS_2_TRANSFER_READ_BIT;

  if (has(access, Access::TransferWrite))
    result |= VK_ACCESS_2_TRANSFER_WRITE_BIT;

  if (has(access, Access::HostRead))
    result |= VK_ACCESS_2_HOST_READ_BIT;

  if (has(access, Access::HostWrite))
    result |= VK_ACCESS_2_HOST_WRITE_BIT;

  if (has(access, Access::MemoryRead))
    result |= VK_ACCESS_2_MEMORY_READ_BIT;

  if (has(access, Access::MemoryWrite))
    result |= VK_ACCESS_2_MEMORY_WRITE_BIT;

  return result;
}

constexpr auto toVkPipelineStage(PipelineStage stage) noexcept -> VkPipelineStageFlags2 {
  VkPipelineStageFlags2 result{};

  if (has(stage, PipelineStage::TopOfPipe))
    result |= VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;

  if (has(stage, PipelineStage::DrawIndirect))
    result |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

  if (has(stage, PipelineStage::VertexInput))
    result |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;

  if (has(stage, PipelineStage::VertexShader))
    result |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;

  if (has(stage, PipelineStage::FragmentShader))
    result |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

  if (has(stage, PipelineStage::EarlyFragmentTests))
    result |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;

  if (has(stage, PipelineStage::LateFragmentTests))
    result |= VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

  if (has(stage, PipelineStage::ColorAttachmentOutput))
    result |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

  if (has(stage, PipelineStage::ComputeShader))
    result |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

  if (has(stage, PipelineStage::Transfer))
    result |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;

  if (has(stage, PipelineStage::BottomOfPipe))
    result |= VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;

  if (has(stage, PipelineStage::Host))
    result |= VK_PIPELINE_STAGE_2_HOST_BIT;

  if (has(stage, PipelineStage::AllGraphics))
    result |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

  if (has(stage, PipelineStage::AllCommands))
    result |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

  return result;
}

constexpr auto toVkPipelineBindPoint(PipelineBindPoint bindPoint) -> VkPipelineBindPoint {
  switch (bindPoint) {
    case PipelineBindPoint::Graphics: return VK_PIPELINE_BIND_POINT_GRAPHICS;
    case PipelineBindPoint::Compute: return VK_PIPELINE_BIND_POINT_COMPUTE;
  }

  return VK_PIPELINE_BIND_POINT_GRAPHICS;
}

constexpr auto fromVkColorSpace(VkColorSpaceKHR colorSpace) -> Option<SurfaceColorSpace> {
  switch (colorSpace) {
    case VkColorSpaceKHR::VK_COLORSPACE_SRGB_NONLINEAR_KHR: return SurfaceColorSpace::SrgbNonlinear;
    default: return None;
  }
}

constexpr auto toVkColorSpace(SurfaceColorSpace colorSpace) -> Option<VkColorSpaceKHR> {
  switch (colorSpace) {
    case SurfaceColorSpace::SrgbNonlinear: return VkColorSpaceKHR::VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    default: return None;
  }
}

constexpr auto fromVkFormat(VkFormat format) noexcept -> Option<TextureFormat> {
  switch (format) {
    case VK_FORMAT_R8_UNORM: return TextureFormat::R8Unorm;
    case VK_FORMAT_R8_SNORM: return TextureFormat::R8Snorm;
    case VK_FORMAT_R8_UINT: return TextureFormat::R8Uint;
    case VK_FORMAT_R8_SINT: return TextureFormat::R8Sint;
    case VK_FORMAT_R8G8_UNORM: return TextureFormat::Rg8Unorm;
    case VK_FORMAT_R8G8_SNORM: return TextureFormat::Rg8Snorm;
    case VK_FORMAT_R8G8_UINT: return TextureFormat::Rg8Uint;
    case VK_FORMAT_R8G8_SINT: return TextureFormat::Rg8Sint;
    case VK_FORMAT_R8G8B8A8_UNORM: return TextureFormat::Rgba8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB: return TextureFormat::Rgba8UnormSrgb;
    case VK_FORMAT_R8G8B8A8_UINT: return TextureFormat::Rgba8Uint;
    case VK_FORMAT_R8G8B8A8_SINT: return TextureFormat::Rgba8Sint;
    case VK_FORMAT_B8G8R8A8_UNORM: return TextureFormat::Bgra8Unorm;
    case VK_FORMAT_B8G8R8A8_SRGB: return TextureFormat::Bgra8UnormSrgb;
    case VK_FORMAT_R16_SFLOAT: return TextureFormat::R16Float;
    case VK_FORMAT_R16G16_SFLOAT: return TextureFormat::Rg16Float;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return TextureFormat::Rgba16Float;
    case VK_FORMAT_R32_SFLOAT: return TextureFormat::R32Float;
    case VK_FORMAT_R32G32_SFLOAT: return TextureFormat::Rg32Float;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return TextureFormat::Rgba32Float;
    case VK_FORMAT_D16_UNORM: return TextureFormat::D16Unorm;
    case VK_FORMAT_D24_UNORM_S8_UINT: return TextureFormat::D24UnormS8Uint;
    case VK_FORMAT_D32_SFLOAT: return TextureFormat::D32Float;
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return TextureFormat::D32FloatS8Uint;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return TextureFormat::Bc1RgbaUnorm;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: return TextureFormat::Bc1RgbaUnormSrgb;
    case VK_FORMAT_BC3_UNORM_BLOCK: return TextureFormat::Bc3Unorm;
    case VK_FORMAT_BC3_SRGB_BLOCK: return TextureFormat::Bc3UnormSrgb;
    case VK_FORMAT_BC5_UNORM_BLOCK: return TextureFormat::Bc5Unorm;
    case VK_FORMAT_BC7_UNORM_BLOCK: return TextureFormat::Bc7Unorm;
    case VK_FORMAT_BC7_SRGB_BLOCK: return TextureFormat::Bc7UnormSrgb;
    case VK_FORMAT_UNDEFINED: return TextureFormat::Undefined;
    default: return None;
  }
}

constexpr auto fromVkPresentMode(VkPresentModeKHR mode) noexcept -> Option<PresentMode> {
  switch (mode) {
    case VK_PRESENT_MODE_FIFO_KHR: return PresentMode::Fifo;
    case VK_PRESENT_MODE_MAILBOX_KHR: return PresentMode::Mailbox;
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return PresentMode::Immediate;
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return PresentMode::FifoRelaxed;
    default: return None;
  }
}

constexpr auto toVkPresentMode(PresentMode mode) noexcept -> VkPresentModeKHR {
  switch (mode) {
    case PresentMode::Fifo: return VK_PRESENT_MODE_FIFO_KHR;
    case PresentMode::Mailbox: return VK_PRESENT_MODE_MAILBOX_KHR;
    case PresentMode::Immediate: return VK_PRESENT_MODE_IMMEDIATE_KHR;
    case PresentMode::FifoRelaxed: return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    default: return VK_PRESENT_MODE_FIFO_KHR;
  }
}

constexpr auto toVkImageViewType(TextureDimension dimension, u32 arrayLayers) noexcept -> VkImageViewType {
  switch (dimension) {
    case TextureDimension::D1: return arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
    case TextureDimension::D2: return arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    case TextureDimension::D3: return VK_IMAGE_VIEW_TYPE_3D;
  }

  return VK_IMAGE_VIEW_TYPE_2D;
}

constexpr auto makeTextureUsage(VkImageUsageFlags usage) noexcept -> TextureUsage {
  TextureUsage result{};

  if ((usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0)
    result |= TextureUsage::TransferSrc;

  if ((usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0)
    result |= TextureUsage::TransferDst;

  if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0)
    result |= TextureUsage::Sampled;

  if ((usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0)
    result |= TextureUsage::Storage;

  if ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0)
    result |= TextureUsage::ColorAttachment;

  if ((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
    result |= TextureUsage::DepthStencilAttachment;

  if ((usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) != 0)
    result |= TextureUsage::InputAttachment;

  if ((usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) != 0)
    result |= TextureUsage::Transient;

  return result;
}

constexpr auto makeFormatFeatureMask(TextureUsage usage) noexcept -> VkFormatFeatureFlags {
  VkFormatFeatureFlags result{};

  if (has(usage, TextureUsage::Sampled))
    result |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

  if (has(usage, TextureUsage::Storage))
    result |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;

  if (has(usage, TextureUsage::ColorAttachment))
    result |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;

  if (has(usage, TextureUsage::DepthStencilAttachment))
    result |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

  return result;
}

constexpr auto toVkShaderStage(ShaderStage stage) noexcept -> Option<VkShaderStageFlagBits> {
  switch (stage) {
    case ShaderStage::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderStage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
  }

  return None;
}

constexpr auto toVkSeverity(DebugMessageSeverity severity) noexcept -> VkDebugUtilsMessageSeverityFlagsEXT {
  VkDebugUtilsMessageSeverityFlagsEXT result{};

  if (has(severity, DebugMessageSeverity::Verbose))
    result |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;

  if (has(severity, DebugMessageSeverity::Info))
    result |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;

  if (has(severity, DebugMessageSeverity::Warning))
    result |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;

  if (has(severity, DebugMessageSeverity::Error))
    result |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

  return result;
}

constexpr auto makeAdapterType(VkPhysicalDeviceType type) noexcept -> AdapterType {
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return AdapterType::Integrated;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return AdapterType::Discrete;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return AdapterType::Virtual;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return AdapterType::Cpu;
    default: return AdapterType::Other;
  }
}

constexpr auto makeQueueCapabilities(VkQueueFlags flags) noexcept -> QueueCapabilities {
  QueueCapabilities result{};

  if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0)
    result |= QueueCapabilities::Graphics;

  if ((flags & VK_QUEUE_COMPUTE_BIT) != 0)
    result |= QueueCapabilities::Compute;

  if ((flags & VK_QUEUE_TRANSFER_BIT) != 0)
    result |= QueueCapabilities::Transfer;

  if ((flags & VK_QUEUE_SPARSE_BINDING_BIT) != 0)
    result |= QueueCapabilities::SparseBinding;

  return result;
}

constexpr auto makeFormatFeatures(VkFormatFeatureFlags flags) noexcept -> TextureFormatFeatures {
  TextureFormatFeatures result{};

  if ((flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0)
    result |= TextureFormatFeatures::Sampled;

  if ((flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0)
    result |= TextureFormatFeatures::Storage;

  if ((flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0)
    result |= TextureFormatFeatures::ColorAttachment;

  if ((flags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
    result |= TextureFormatFeatures::DepthStencilAttachment;

  if ((flags & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0)
    result |= TextureFormatFeatures::BlitSrc;

  if ((flags & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0)
    result |= TextureFormatFeatures::BlitDst;

  if ((flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0)
    result |= TextureFormatFeatures::LinearFilter;

  return result;
}

} // namespace Nyx::RHI::detail

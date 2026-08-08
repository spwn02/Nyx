import Nyx.Core;
import Nyx.RHI;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;
using namespace Nyx::RHI;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::RHI {

[[ = test, = group("core"), = tag("rhi") ]] auto textureFormatMetadata() -> void {
  check(texture_format_info_v<TextureFormat::Rgba8Unorm>.blockBytes == 4);
  check(texture_format_info_v<TextureFormat::Rgba8UnormSrgb>.srgb);
  check(texture_format_info_v<TextureFormat::D32Float>.aspects == TextureAspect::Depth);
  check(texture_format_info_v<TextureFormat::D24UnormS8Uint>.aspects ==
        (TextureAspect::Depth | TextureAspect::Stencil));
  check(textureByteSize(TextureFormat::Rgba8Unorm, {.width = 128, .height = 128, .depth = 1}, 1, 1) ==
        static_cast<u64>(128) * 128 * 4);
  check(not TextureHandle{}.valid());
}

} // namespace Tests::RHI
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::RHI>();
}

#include <catch2/catch_test_macros.hpp>

import Nyx.Core;
import Nyx.RHI;

namespace Nyx::RHI {

TEST_CASE("Texture format metadata") {
  REQUIRE(texture_format_info_v<TextureFormat::Rgba8Unorm>.blockBytes == 4);
  REQUIRE(texture_format_info_v<TextureFormat::Rgba8UnormSrgb>.srgb);
  REQUIRE(texture_format_info_v<TextureFormat::D32Float>.aspects == TextureAspect::Depth);
  REQUIRE(texture_format_info_v<TextureFormat::D24UnormS8Uint>.aspects ==
          (TextureAspect::Depth | TextureAspect::Stencil));
  REQUIRE(textureByteSize(TextureFormat::Rgba8Unorm, {.width = 128, .height = 128, .depth = 1}, 1, 1) ==
          static_cast<u64>(128) * 128 * 4);
  REQUIRE(TextureHandle{}.valid() == false);
}

}

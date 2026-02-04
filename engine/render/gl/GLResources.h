#pragma once

#include "render/rg/RGDesc.h"
#include "render/rg/RGFormat.h"
#include <cstdint>
#include <string>

namespace Nyx {

struct GLTexture2D {
  uint32_t tex = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t layers = 1;
  uint32_t mips = 1;
  uint32_t target = 0;
  RGFormat format = RGFormat::RGBA8;
};

struct GLBuffer {
  uint32_t buf = 0;
  uint32_t byteSize = 0;
};

struct GLFramebuffer {
  uint32_t fbo = 0;
};

class GLResources final {
public:
  GLTexture2D acquireTexture2D(const RGTexDesc &desc);
  void releaseTexture2D(GLTexture2D &t);

  uint32_t acquireFBO();
  void releaseFBO(uint32_t &fbo);

  static uint32_t glInternalFormat(RGFormat f);
  static uint32_t glFormat(RGFormat f);
  static uint32_t glType(RGFormat f);

  uint32_t createTexture2DFromFile(const std::string &path, bool srgb);
};

} // namespace Nyx

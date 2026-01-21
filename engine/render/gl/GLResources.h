#pragma once

#include "render/rg/RGDesc.h"
#include "render/rg/RGFormat.h"
#include <cstdint>

namespace Nyx {

struct GLTexture2D {
  uint32_t tex = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  RGFormat format = RGFormat::RGBA8;
};

struct GLFramebuffer {
  uint32_t fbo = 0;
};

class GLResources final {
public:
  ~GLResources();

  GLTexture2D acquireTexture2D(const RGTexDesc &desc);
  void releaseTexture2D(GLTexture2D &t);

  uint32_t acquireFBO();
  void releaseFBO(uint32_t &fbo);

  static uint32_t glInternalFormat(RGFormat f);
  static uint32_t glFormat(RGFormat f);
  static uint32_t glType(RGFormat f);
};

} // namespace Nyx

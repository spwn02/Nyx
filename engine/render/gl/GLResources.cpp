#include "GLResources.h"
#include "render/rg/RGFormat.h"

#include "../../core/Assert.h"
#include <glad/glad.h>

namespace Nyx {

GLResources::~GLResources() {}

uint32_t GLResources::glInternalFormat(RGFormat f) {
  switch (f) {
  case RGFormat::RGBA16F:
    return GL_RGBA16F;
  case RGFormat::RGBA8:
    return GL_RGBA8;
  case RGFormat::Depth32F:
    return GL_DEPTH_COMPONENT32F;
  case RGFormat::R32UI:
    return GL_R32UI;
  default:
    return GL_RGBA8;
  }
}

uint32_t GLResources::glFormat(RGFormat f) {
  switch (f) {
  case RGFormat::Depth32F:
    return GL_DEPTH_COMPONENT;
  case RGFormat::R32UI:
    return GL_RED_INTEGER;
  default:
    return GL_RGBA;
  }
}

uint32_t GLResources::glType(RGFormat f) {
  switch (f) {
  case RGFormat::RGBA16F:
    return GL_HALF_FLOAT;
  case RGFormat::Depth32F:
    return GL_FLOAT;
  case RGFormat::R32UI:
    return GL_UNSIGNED_INT;
  default:
    return GL_UNSIGNED_BYTE;
  }
}

GLTexture2D GLResources::acquireTexture2D(const RGTexDesc &desc) {
  NYX_ASSERT(desc.w > 0 && desc.h > 0,
             "acquireTexture2D invalid size");
  
  GLTexture2D tex{};
  tex.width = desc.w;
  tex.height = desc.h;
  tex.format = desc.fmt;

  glCreateTextures(GL_TEXTURE_2D, 1, &tex.tex);

  const bool isInteger =
      (desc.fmt == RGFormat::R32UI);
  const GLenum filter = isInteger ? GL_NEAREST : GL_LINEAR;
  glTextureParameteri(tex.tex, GL_TEXTURE_MIN_FILTER, filter);
  glTextureParameteri(tex.tex, GL_TEXTURE_MAG_FILTER, filter);
  glTextureParameteri(tex.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTextureParameteri(tex.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  const GLenum internalFormat = glInternalFormat(desc.fmt);
  const GLenum format = glFormat(desc.fmt);
  const GLenum type = glType(desc.fmt);

  glTextureStorage2D(tex.tex, 1, internalFormat, desc.w, desc.h);
  (void)format;
  (void)type;

  return tex;
}

void GLResources::releaseTexture2D(GLTexture2D &t) {
  if (t.tex != 0) {
    glDeleteTextures(1, &t.tex);
    t.tex = 0;
  }
  t.width = t.height = 0;
  t.format = RGFormat::RGBA8;
}

uint32_t GLResources::acquireFBO() {
  uint32_t fbo = 0;
  glCreateFramebuffers(1, &fbo);
  return fbo;
}

void GLResources::releaseFBO(uint32_t &fbo) {
  if (fbo != 0) {
    glDeleteFramebuffers(1, &fbo);
    fbo = 0;
  }
}

} // namespace Nyx

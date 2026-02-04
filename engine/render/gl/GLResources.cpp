#include "GLResources.h"
#include "render/rg/RGFormat.h"

#include "core/Assert.h"
#include "core/Log.h"
#include <glad/glad.h>

#include <stb_image.h>

namespace Nyx {

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
  case RGFormat::R32F:
    return GL_R32F;
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
  case RGFormat::R32F:
    return GL_RED;
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
  case RGFormat::R32F:
    return GL_FLOAT;
  default:
    return GL_UNSIGNED_BYTE;
  }
}

GLTexture2D GLResources::acquireTexture2D(const RGTexDesc &desc) {
  NYX_ASSERT(desc.w > 0 && desc.h > 0, "acquireTexture2D invalid size");

  GLTexture2D tex{};
  tex.width = desc.w;
  tex.height = desc.h;
  tex.layers = std::max(1u, desc.layers);
  tex.mips = std::max(1u, desc.mips);
  tex.format = desc.fmt;

  const bool isArray = tex.layers > 1;
  tex.target = isArray ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
  glCreateTextures(tex.target, 1, &tex.tex);

  const bool isInteger = (desc.fmt == RGFormat::R32UI);
  const GLenum magFilter = isInteger ? GL_NEAREST : GL_LINEAR;
  GLenum minFilter = magFilter;
  if (tex.mips > 1) {
    minFilter = isInteger ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
  }
  glTextureParameteri(tex.tex, GL_TEXTURE_MIN_FILTER, minFilter);
  glTextureParameteri(tex.tex, GL_TEXTURE_MAG_FILTER, magFilter);
  glTextureParameteri(tex.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTextureParameteri(tex.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (isArray)
    glTextureParameteri(tex.tex, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  const GLenum internalFormat = glInternalFormat(desc.fmt);
  const GLenum format = glFormat(desc.fmt);
  const GLenum type = glType(desc.fmt);

  if (isArray) {
    glTextureStorage3D(tex.tex, (GLsizei)tex.mips, internalFormat, desc.w,
                       desc.h, tex.layers);
  } else {
    glTextureStorage2D(tex.tex, (GLsizei)tex.mips, internalFormat, desc.w,
                       desc.h);
  }
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
  t.layers = 1;
  t.format = RGFormat::RGBA8;
  t.target = 0;
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

uint32_t GLResources::createTexture2DFromFile(const std::string &path,
                                              bool srgb) {
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc *data =
      stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
  if (!data) {
    Log::Error("Failed to load texture from file: {}", path);
    return 0;
  }

  uint32_t glTex = 0;
  glGenTextures(1, &glTex);
  glBindTexture(GL_TEXTURE_2D, glTex);

  GLenum internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
  glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);

  glGenerateMipmap(GL_TEXTURE_2D);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  stbi_image_free(data);
  return glTex;
}

} // namespace Nyx

#include "EnvironmentIBL.h"
#include "core/Assert.h"
#include "core/Log.h"
#include "render/gl/GLShaderUtil.h"

#include <algorithm>
#include <cstdint>
#include <glad/glad.h>
#include <string>

#include <stb_image.h>

namespace Nyx {

static void createCubeRGBA16F(uint32_t &tex, uint32_t size, bool mipmapped) {
  if (!tex)
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &tex);

  const uint32_t mips = mipmapped ? EnvironmentIBL::mipCountForSize(size) : 1u;

  glTextureStorage2D(tex, static_cast<GLint>(mips), GL_RGBA16F,
                     static_cast<GLsizei>(size), static_cast<GLsizei>(size));
  glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER,
                      mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

static void create2DRG16F(uint32_t &tex, uint32_t w, uint32_t h) {
  if (!tex)
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
  glTextureStorage2D(tex, 1, GL_RG16F, static_cast<GLsizei>(w),
                     static_cast<GLsizei>(h));
  glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

uint32_t EnvironmentIBL::mipCountForSize(uint32_t s) {
  uint32_t m = 1;
  while (s > 1) {
    s >>= 1;
    ++m;
  }
  return m;
}

void EnvironmentIBL::destroyTex(uint32_t &t) {
  if (t) {
    glDeleteTextures(1, &t);
    t = 0;
  }
}

void EnvironmentIBL::init(GLShaderUtil &shaders) {
  m_shaders = &shaders;
  // Start clean; we only build when an HDRI is assigned.
  m_dirty = false;
  m_ready = false;
}

void EnvironmentIBL::shutdown() {
  destroyTex(m_envCube);
  destroyTex(m_irrCube);
  destroyTex(m_prefilterCube);
  destroyTex(m_brdfLUT);

  m_hdrEquirect = 0;
  m_hdrWidth = m_hdrHeight = 0;
  m_dirty = true;
  m_ready = false;
  m_shaders = nullptr;
}

void EnvironmentIBL::setHDRI(uint32_t hdrTex, uint32_t hdrW, uint32_t hdrH,
                             const std::string &debugName) {
  if (m_hdrEquirect == hdrTex && m_hdrWidth == hdrW && m_hdrHeight == hdrH &&
      m_hdrName == debugName)
    return;

  m_hdrEquirect = hdrTex;
  m_hdrWidth = hdrW;
  m_hdrHeight = hdrH;
  m_hdrName = debugName;
  m_dirty = true;
  m_ready = false;
}

void EnvironmentIBL::loadFromHDR(const std::string &path) {
  int w, h, c;
  // stbi_set_flip_vertically_on_load(true);
  unsigned char *data = stbi_load(path.c_str(), &w, &h, &c, 4);
  if (data) {
    uint32_t hdrTex;
    glCreateTextures(GL_TEXTURE_2D, 1, &hdrTex);
    glTextureStorage2D(hdrTex, 1, GL_RGBA16F, w, h);
    glTextureSubImage2D(hdrTex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTextureParameteri(hdrTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(hdrTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(hdrTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(hdrTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    setHDRI(hdrTex, static_cast<uint32_t>(w), static_cast<uint32_t>(h), path);

    stbi_image_free(data);
  } else {
    // Log::Warn("EnvironmentIBL: Failed to load HDRI from {}", path);
  }
}

void EnvironmentIBL::ensureBuilt() {
  if (!m_dirty)
    return;
  if (!m_hdrEquirect) {
    Log::Warn("EnvironmentIBL: no HDRI set; skipping build");
    m_dirty = false;
    return;
  }
  NYX_ASSERT(m_shaders != nullptr, "EnvironmentIBL::init() must be called");

  createOrResizeResources();

  dispatchEquirectToCube();
  dispatchIrradiance();
  dispatchPrefilter();
  dispatchBRDFLUT();

  m_dirty = false;
  m_ready = true;
}

void EnvironmentIBL::ensureResources() {
  if (!m_hdrEquirect)
    return;
  createOrResizeResources();
}

void EnvironmentIBL::markBuilt() {
  m_dirty = false;
  m_ready = true;
}

void EnvironmentIBL::createOrResizeResources() {
  // Radiance cube (mipmapped, later used for sky).
  createCubeRGBA16F(m_envCube, m_settings.cubeSize, true);

  // Irradiance cube (no need for mips)
  createCubeRGBA16F(m_irrCube, m_settings.irrSize, false);

  // Prefilter cube (mipmapped required)
  createCubeRGBA16F(m_prefilterCube, m_settings.prefilterSize, true);

  // BRDF LUT
  create2DRG16F(m_brdfLUT, m_settings.brdfSize, m_settings.brdfSize);
}

void EnvironmentIBL::dispatchEquirectToCube() {
  const uint32_t prog = m_shaders->buildProgramC("env_equirect_to_cube.comp");
  NYX_ASSERT(prog != 0, "env_equirect_to_cube.comp compile failed");

  glUseProgram(prog);

  // Input: HDR equirect sampler2D at binding=0
  glBindTextureUnit(0, m_hdrEquirect);

  // Output: writeonly imageCube at binding=1 (level 0)
  glBindImageTexture(1, m_envCube, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

  const uint32_t size = m_settings.cubeSize;
  const uint32_t gx = (size + 7u) / 8u;
  const uint32_t gy = (size + 7u) / 8u;

  // z dimension = 6 faces
  glDispatchCompute(gx, gy, 6);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                  GL_TEXTURE_FETCH_BARRIER_BIT);

  // Generate mips for envCube
  glGenerateTextureMipmap(m_envCube);
}

void EnvironmentIBL::dispatchIrradiance() {
  const uint32_t prog = m_shaders->buildProgramC("env_irradiance.comp");
  NYX_ASSERT(prog != 0, "env_irradiance.comp compile failed");

  glUseProgram(prog);

  // Input: radiance cube samplerCube binding=0
  glBindTextureUnit(0, m_envCube);

  // Output: irradiance imageCube binding=1
  glBindImageTexture(1, m_irrCube, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

  const uint32_t size = m_settings.irrSize;
  const uint32_t gx = (size + 7u) / 8u;
  const uint32_t gy = (size + 7u) / 8u;
  glDispatchCompute(gx, gy, 6);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                  GL_TEXTURE_FETCH_BARRIER_BIT);
}

void EnvironmentIBL::dispatchPrefilter() {
  const uint32_t prog = m_shaders->buildProgramC("env_prefilter.comp");
  NYX_ASSERT(prog != 0, "env_prefilter.comp compile failed");

  glUseProgram(prog);

  glBindTextureUnit(0, m_envCube);

  // Prefilter writes per-mip level.
  const uint32_t baseSize = m_settings.prefilterSize;
  const uint32_t mipCount = mipCountForSize(baseSize);

  const GLint locSamples = glGetUniformLocation(prog, "u_SampleCount");
  if (locSamples >= 0)
    glUniform1ui(locSamples, m_settings.sampleCount);

  for (uint32_t mip = 0; mip < mipCount; ++mip) {
    const uint32_t sz = std::max(1u, baseSize >> mip);
    const float roughness =
        mipCount <= 1
            ? 0.0f
            : static_cast<float>(mip) / static_cast<float>(mipCount - 1);

    const GLint locR = glGetUniformLocation(prog, "u_Roughness");
    if (locR >= 0)
      glUniform1f(locR, roughness);

    // bind output mip as imageCube
    glBindImageTexture(1, m_prefilterCube, static_cast<GLint>(mip), GL_TRUE, 0,
                       GL_WRITE_ONLY, GL_RGBA16F);

    const uint32_t gx = (sz + 7u) / 8u;
    const uint32_t gy = (sz + 7u) / 8u;
    glDispatchCompute(gx, gy, 6);

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT);
  }
}

void EnvironmentIBL::dispatchBRDFLUT() {
  const uint32_t prog = m_shaders->buildProgramC("env_brdf_lut.comp");
  NYX_ASSERT(prog != 0, "env_brdf_lut.comp compile failed");

  glUseProgram(prog);

  glBindImageTexture(0, m_brdfLUT, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);

  const uint32_t sz = m_settings.brdfSize;
  const uint32_t gx = (sz + 7u) / 8u;
  const uint32_t gy = (sz + 7u) / 8u;
  glDispatchCompute(gx, gy, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                  GL_TEXTURE_FETCH_BARRIER_BIT);
}

} // namespace Nyx

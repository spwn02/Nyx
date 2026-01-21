#include "PassTonemap.h"
#include "../../core/Assert.h"
#include "../../core/Log.h"

#include <glad/glad.h>

namespace Nyx {

static const char *kTonemapCS = R"GLSL(
#version 460 core
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding=0) uniform sampler2D u_HDR;
layout(rgba8, binding=1) uniform writeonly image2D u_LDR;

uniform float u_Exposure = 1.0;
uniform int   u_ApplyGamma = 1;

vec3 acesFitted(vec3 x) {
  // Narkowicz ACES approximation (fast, looks good)
  // https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
  ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
  ivec2 sz = imageSize(u_LDR);
  if (pix.x >= sz.x || pix.y >= sz.y) return;

  vec3 hdr = texelFetch(u_HDR, pix, 0).rgb;

  // Exposure (stub, later from project/view settings)
  hdr *= max(u_Exposure, 0.0);

  // Tonemap
  vec3 ldr = acesFitted(hdr);

  // If LDR is stored as RGBA8 (linear), but later treated as display-ready:
  // apply gamma here. If you later switch to SRGB8 texture, set u_ApplyGamma=0.
  if (u_ApplyGamma != 0) {
    ldr = pow(ldr, vec3(1.0/2.2));
  }

  imageStore(u_LDR, pix, vec4(ldr, 1.0));
}
)GLSL";

static uint32_t compileCS(const char *src) {
  const uint32_t sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);

  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
    std::string log;
    log.resize((size_t)std::max(0, len));
    glGetShaderInfoLog(sh, len, nullptr, log.data());
    Log::Error("Tonemap CS compile failed:\n{}", log);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

static uint32_t linkCompute(uint32_t cs) {
  NYX_ASSERT(cs != 0, "linkCompute: invalid shader");
  const uint32_t p = glCreateProgram();
  glAttachShader(p, cs);
  glLinkProgram(p);

  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
    std::string log;
    log.resize((size_t)std::max(0, len));
    glGetProgramInfoLog(p, len, nullptr, log.data());
    Log::Error("Tonemap program link failed:\n{}", log);
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

void PassTonemap::init() {
  const uint32_t cs = compileCS(kTonemapCS);
  NYX_ASSERT(cs != 0, "PassTonemap CS compile failed");
  m_prog = linkCompute(cs);
  glDeleteShader(cs);
  NYX_ASSERT(m_prog != 0, "PassTonemap program link failed");
}

void PassTonemap::shutdown() {
  if (m_prog) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassTonemap::dispatch(uint32_t hdrTex, uint32_t ldrTex, uint32_t width,
                           uint32_t height, float exposure, bool applyGamma) {
  NYX_ASSERT(m_prog != 0, "PassTonemap not initialized");
  NYX_ASSERT(hdrTex != 0 && ldrTex != 0, "PassTonemap invalid textures");

  glUseProgram(m_prog);

  // uniforms
  const GLint locExp = glGetUniformLocation(m_prog, "u_Exposure");
  const GLint locGam = glGetUniformLocation(m_prog, "u_ApplyGamma");
  if (locExp >= 0)
    glUniform1f(locExp, exposure);
  if (locGam >= 0)
    glUniform1i(locGam, applyGamma ? 1 : 0);

  // input sampler
  glBindTextureUnit(0, hdrTex);

  // output image
  glBindImageTexture(1, ldrTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

  const uint32_t gx = (width + 15u) / 16u;
  const uint32_t gy = (height + 15u) / 16u;
  glDispatchCompute(gx, gy, 1);

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                  GL_TEXTURE_FETCH_BARRIER_BIT);
}

} // namespace Nyx

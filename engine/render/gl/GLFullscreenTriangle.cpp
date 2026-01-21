#include "GLFullscreenTriangle.h"
#include "../../core/Assert.h"
#include <glad/glad.h>

namespace Nyx {

void GLFullscreenTriangle::init() {
  if (!vao)
    glCreateVertexArrays(1, &vao);
}

void GLFullscreenTriangle::shutdown() {
  if (vao) {
    glDeleteVertexArrays(1, &vao);
    vao = 0;
  }
}

uint32_t compileShader(uint32_t type, const char *src) {
  uint32_t sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);

  int ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(sh, 4096, nullptr, log);
    fprintf(stderr, "SHADER COMPILE ERROR:\n%s\n", log);
    glDeleteShader(sh);
    NYX_ASSERT(false, log);
  }
  
  // Verify shader was compiled
  int len = 0;
  glGetShaderiv(sh, GL_SHADER_SOURCE_LENGTH, &len);
  
  return sh;
}

uint32_t linkProgram(uint32_t vs, uint32_t fs) {
  uint32_t p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);

  int ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(p, 4096, nullptr, log);
    glDeleteProgram(p);
    NYX_ASSERT(false, log);
  }
  return p;
}

uint32_t linkProgramCompute(uint32_t cs) {
  uint32_t p = glCreateProgram();
  glAttachShader(p, cs);
  glLinkProgram(p);

  int ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(p, 4096, nullptr, log);
    glDeleteProgram(p);
    NYX_ASSERT(false, log);
  }
  return p;
}

} // namespace Nyx

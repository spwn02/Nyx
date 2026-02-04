#include "GLShaderUtil.h"

#include "core/Log.h"

#include <glad/glad.h>

namespace Nyx {

std::string GLShaderUtil::stageName(uint32_t glStage) {
  switch (glStage) {
  case GL_VERTEX_SHADER:
    return "Vertex";
  case GL_FRAGMENT_SHADER:
    return "Fragment";
  case GL_COMPUTE_SHADER:
    return "Compute";
  case GL_GEOMETRY_SHADER:
    return "Geometry";
  case GL_TESS_CONTROL_SHADER:
    return "TessControl";
  case GL_TESS_EVALUATION_SHADER:
    return "TessEval";
  default:
    return "UnknownStage";
  }
}

std::string GLShaderUtil::getShaderInfoLog(uint32_t shader) {
  GLint len = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
  if (len <= 1)
    return {};

  std::string log;
  log.resize((size_t)len);
  GLsizei outLen = 0;
  glGetShaderInfoLog(shader, len, &outLen, log.data());
  if (outLen > 0)
    log.resize((size_t)outLen);
  return log;
}

std::string GLShaderUtil::getProgramInfoLog(uint32_t prog) {
  GLint len = 0;
  glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
  if (len <= 1)
    return {};

  std::string log;
  log.resize((size_t)len);
  GLsizei outLen = 0;
  glGetProgramInfoLog(prog, len, &outLen, log.data());
  if (outLen > 0)
    log.resize((size_t)outLen);
  return log;
}

uint32_t GLShaderUtil::compileFromSource(uint32_t glStage,
                                         const std::string &source,
                                         const std::string &debugName) {
  if (source.empty()) {
    Log::Error("GLShaderUtil: empty source for {} shader: {}",
               stageName(glStage), debugName);
    return 0;
  }

  uint32_t sh = glCreateShader(glStage);

  const char *src = source.c_str();
  GLint srclen = (GLint)source.size();
  glShaderSource(sh, 1, &src, &srclen);
  glCompileShader(sh);

  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);

  const std::string log = getShaderInfoLog(sh);

  if (!ok) {
    Log::Error("Shader compile FAILED ({}): {}", stageName(glStage), debugName);
    if (!log.empty())
      Log::Error("Shader log:\n{}", log);
    glDeleteShader(sh);
    return 0;
  }

  if (!log.empty()) {
    // Warnings are useful
    Log::Warn("Shader compile log ({}): {}\n{}", stageName(glStage), debugName,
              log);
  }

  return sh;
}

uint32_t GLShaderUtil::compileFromFile(uint32_t glStage,
                                       const std::string &relativePath) {
  auto r = m_loader.loadExpanded(relativePath);
  if (!r.ok) {
    Log::Error("GLShaderUtil: loadExpanded failed: {}\n{}", relativePath,
               r.error);
    return 0;
  }
  return compileFromSource(glStage, r.expandedSource, relativePath);
}

uint32_t GLShaderUtil::linkProgram(uint32_t vs, uint32_t fs) {
  if (!vs || !fs) {
    Log::Error(
        "GLShaderUtil::linkProgram called with null shader(s). vs={} fs={}", vs,
        fs);
    return 0;
  }

  uint32_t prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);

  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);

  const std::string log = getProgramInfoLog(prog);

  if (!ok) {
    Log::Error("Program link FAILED (VS+FS).");
    if (!log.empty())
      Log::Error("Program log:\n{}", log);
    glDeleteProgram(prog);
    return 0;
  }

  if (!log.empty()) {
    Log::Warn("Program link log (VS+FS):\n{}", log);
  }

  // Detach is optional; safe cleanup
  glDetachShader(prog, vs);
  glDetachShader(prog, fs);

  return prog;
}

uint32_t GLShaderUtil::linkProgramCompute(uint32_t cs) {
  if (!cs) {
    Log::Error(
        "GLShaderUtil::linkProgramCompute called with null shader. cs={}", cs);
    return 0;
  }

  uint32_t prog = glCreateProgram();
  glAttachShader(prog, cs);
  glLinkProgram(prog);

  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);

  const std::string log = getProgramInfoLog(prog);

  if (!ok) {
    Log::Error("Program link FAILED (CS).");
    if (!log.empty())
      Log::Error("Program log:\n{}", log);
    glDeleteProgram(prog);
    return 0;
  }

  if (!log.empty()) {
    Log::Warn("Program link log (CS):\n{}", log);
  }

  glDetachShader(prog, cs);
  return prog;
}

uint32_t GLShaderUtil::buildProgramVF(const std::string &vsPath,
                                      const std::string &fsPath) {
  uint32_t vs = compileFromFile(GL_VERTEX_SHADER, vsPath);
  uint32_t fs = compileFromFile(GL_FRAGMENT_SHADER, fsPath);
  if (!vs || !fs) {
    if (vs)
      glDeleteShader(vs);
    if (fs)
      glDeleteShader(fs);
    return 0;
  }

  uint32_t prog = linkProgram(vs, fs);
  glDeleteShader(vs);
  glDeleteShader(fs);
  return prog;
}

uint32_t GLShaderUtil::buildProgramC(const std::string &csPath) {
  uint32_t cs = compileFromFile(GL_COMPUTE_SHADER, csPath);
  if (!cs)
    return 0;

  uint32_t prog = linkProgramCompute(cs);
  glDeleteShader(cs);
  return prog;
}

} // namespace Nyx

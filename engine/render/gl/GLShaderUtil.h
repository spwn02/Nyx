#pragma once

#include "ShaderSourceLoader.h"

#include <cstdint>
#include <string>

namespace Nyx {

// Centralized OpenGL shader compilation/link helpers.
// - takes GLSL sources from ShaderSourceLoader (files + includes)
// - prints full logs on failure
// - returns 0 on failure
class GLShaderUtil final {
public:
  GLShaderUtil() = default;

  void setShaderRoot(std::string rootDir) {
    m_loader.setRoot(std::move(rootDir));
  }
  const std::string &shaderRoot() const { return m_loader.root(); }

  ShaderSourceLoader &loader() { return m_loader; }
  const ShaderSourceLoader &loader() const { return m_loader; }

  // Compile from already-expanded GLSL source string.
  static uint32_t compileFromSource(uint32_t glStage, const std::string &source,
                                    const std::string &debugName);

  // Compile from shader file under shader root, expanding includes.
  uint32_t compileFromFile(uint32_t glStage, const std::string &relativePath);

  // Link programs
  static uint32_t linkProgram(uint32_t vs, uint32_t fs);
  static uint32_t linkProgramCompute(uint32_t cs);

  // Convenience: build full programs from files (deletes shader objects).
  uint32_t buildProgramVF(const std::string &vsPath, const std::string &fsPath);
  uint32_t buildProgramC(const std::string &csPath);

private:
  ShaderSourceLoader m_loader;

  static std::string stageName(uint32_t glStage);
  static std::string getShaderInfoLog(uint32_t shader);
  static std::string getProgramInfoLog(uint32_t prog);
};

} // namespace Nyx

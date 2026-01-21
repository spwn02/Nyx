#pragma once

#include <cstdint>
#include <string>

namespace Nyx {

// A VAO with no VBO; vertex shader generates positions using gl_VertexID.
struct GLFullscreenTriangle {
  uint32_t vao = 0;

  void init();
  void shutdown();
};

uint32_t compileShader(uint32_t type, const char *src);
inline uint32_t compileShader(uint32_t type, const std::string &src) {
  return compileShader(type, src.c_str());
}
uint32_t linkProgram(uint32_t vs, uint32_t fs);
uint32_t linkProgramCompute(uint32_t cs);

} // namespace Nyx

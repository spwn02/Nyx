#pragma once

#include "npgms/MeshCPU.h"
#include <cstdint>

namespace Nyx {

class GLMesh final {
public:
  ~GLMesh();

  void upload(const MeshCPU &cpu);
  void draw() const;
  void drawBaseInstance(uint32_t baseInstance) const;

private:
  uint32_t m_vao = 0;
  uint32_t m_vbo = 0;
  uint32_t m_ebo = 0;
  uint32_t m_indexCount = 0;
};

} // namespace Nyx

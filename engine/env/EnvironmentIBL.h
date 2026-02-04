#pragma once

#include <cstdint>
#include <string>

namespace Nyx {

class GLShaderUtil;

// Persistent environment IBL assets (engine-owned)
class EnvironmentIBL final {
public:
  struct Settings {
    uint32_t cubeSize = 512;      // radiance cube resolution
    uint32_t irrSize = 32;        // irradiance resolution
    uint32_t prefilterSize = 256; // prefilter base resolution
    uint32_t brdfSize = 256;      // BRDF LUT resolution
    uint32_t sampleCount = 1024;  // for prefilter importance sampling
  };

  void init(GLShaderUtil &shaders);
  void shutdown();

  // HDRI source (equirect) is provided by called (asset system).
  // hdrTex must be a GL texture2D handle (ideally RGBA16F).
  void setHDRI(uint32_t hdrTex, uint32_t hdrW, uint32_t hdrH,
               const std::string &debugName);

  void loadFromHDR(const std::string &path);

  // Build cubemaps/LUT if dirty. Called once per frame.
  void ensureBuilt();
  void ensureResources();
  void markBuilt();

  bool ready() const { return m_ready; }
  bool dirty() const { return m_dirty; }

  // Bindings for ForwardMRT.
  uint32_t envCube() const { return m_envCube; }
  uint32_t envIrradianceCube() const { return m_irrCube; }
  uint32_t envPrefilteredCube() const { return m_prefilterCube; }
  uint32_t brdfLUT() const { return m_brdfLUT; }

  uint32_t hdrEquirect() const { return m_hdrEquirect; }
  uint32_t hdrWidth() const { return m_hdrWidth; }
  uint32_t hdrHeight() const { return m_hdrHeight; }

  static uint32_t mipCountForSize(uint32_t s);

private:
  Settings m_settings{};
  GLShaderUtil *m_shaders = nullptr;

  // source
  uint32_t m_hdrEquirect = 0;
  uint32_t m_hdrWidth = 0;
  uint32_t m_hdrHeight = 0;
  std::string m_hdrName;

  // persistent outputs
  uint32_t m_envCube = 0;       // radiance
  uint32_t m_irrCube = 0;       // diffuse irradiance
  uint32_t m_prefilterCube = 0; // spec prefilter (mips)
  uint32_t m_brdfLUT = 0;       // BRDF integration LUT

  bool m_dirty = true;
  bool m_ready = false;

  void createOrResizeResources();
  void dispatchEquirectToCube();
  void dispatchIrradiance();
  void dispatchPrefilter();
  void dispatchBRDFLUT();

  static void destroyTex(uint32_t &t);
};

} // namespace Nyx

#include "scene/NyxSceneIO.h"

#include "io/BinaryIO.h"
#include "io/FileUtil.h"

#include <vector>

namespace Nyx {

static constexpr uint32_t SCENE_MAGIC =
    (uint32_t('N')) | (uint32_t('Y') << 8u) | (uint32_t('X') << 16u) |
    (uint32_t('S') << 24u);

bool NyxSceneIO::save(const NyxScene &scene, const std::string &path,
                      std::string *outError) {
  if (outError)
    outError->clear();

  BinaryWriter w;

  const uint32_t outMajor = scene.header.versionMajor;
  const uint32_t outMinor = (scene.header.versionMinor < 1u) ? 1u
                                                              : scene.header.versionMinor;

  w.writeU32(SCENE_MAGIC);
  w.writeU32(outMajor);
  w.writeU32(outMinor);

  w.writeStringU32(scene.name);
  w.writeStringU32(scene.skyAsset);
  w.writeF32(scene.exposure);

  w.writeU32((uint32_t)scene.entities.size());

  for (const SceneEntity &e : scene.entities) {
    w.writeU64(e.id);
    w.writeStringU32(e.name);

    w.writeF32(e.transform.tx);
    w.writeF32(e.transform.ty);
    w.writeF32(e.transform.tz);
    w.writeF32(e.transform.rx);
    w.writeF32(e.transform.ry);
    w.writeF32(e.transform.rz);
    w.writeF32(e.transform.rw);
    w.writeF32(e.transform.sx);
    w.writeF32(e.transform.sy);
    w.writeF32(e.transform.sz);

    w.writeU64(e.hierarchy.parent);

    w.writeU8(e.hasCamera ? 1u : 0u);
    if (e.hasCamera) {
      w.writeF32(e.camera.fovY);
      w.writeF32(e.camera.nearZ);
      w.writeF32(e.camera.farZ);
      w.writeF32(e.camera.aperture);
      w.writeF32(e.camera.focusDistance);
      w.writeF32(e.camera.sensorWidth);
      w.writeF32(e.camera.sensorHeight);
      w.writeU8(e.camera.active ? 1u : 0u);
    }

    w.writeU8(e.hasLight ? 1u : 0u);
    if (e.hasLight) {
      w.writeU8((uint8_t)e.light.type);
      w.writeF32(e.light.color[0]);
      w.writeF32(e.light.color[1]);
      w.writeF32(e.light.color[2]);
      w.writeF32(e.light.intensity);
      w.writeF32(e.light.range);
      w.writeF32(e.light.spotAngle);
    }

    w.writeU8(e.hasRenderable ? 1u : 0u);
    if (e.hasRenderable) {
      w.writeStringU32(e.renderable.meshAsset);
      w.writeStringU32(e.renderable.materialAsset);
    }
  }

  const auto &blob = w.data();
  return FileUtil::writeFileBytesAtomic(path, blob.data(), blob.size(), outError);
}

bool NyxSceneIO::load(NyxScene &outScene, const std::string &path,
                      std::string *outError) {
  if (outError)
    outError->clear();

  outScene = NyxScene{};

  std::vector<uint8_t> bytes;
  if (!FileUtil::readFileBytes(path, bytes)) {
    if (outError)
      *outError = "Failed to read scene file";
    return false;
  }

  BinaryReader r(bytes.data(), bytes.size());

  auto fail = [&](const char *msg) {
    if (outError)
      *outError = msg;
    return false;
  };

  uint32_t magic = 0;
  if (!r.readU32(magic) || magic != SCENE_MAGIC)
    return fail("Invalid .nyxscene magic");

  if (!r.readU32(outScene.header.versionMajor) ||
      !r.readU32(outScene.header.versionMinor)) {
    return fail("Failed to read .nyxscene version");
  }

  if (!r.readStringU32(outScene.name) || !r.readStringU32(outScene.skyAsset) ||
      !r.readF32(outScene.exposure)) {
    return fail("Failed to read .nyxscene header payload");
  }

  uint32_t entityCount = 0;
  if (!r.readU32(entityCount))
    return fail("Failed to read .nyxscene entity count");

  outScene.entities.resize(entityCount);

  for (SceneEntity &e : outScene.entities) {
    if (!r.readU64(e.id) || !r.readStringU32(e.name))
      return fail("Failed to read entity base fields");

    if (!r.readF32(e.transform.tx) || !r.readF32(e.transform.ty) ||
        !r.readF32(e.transform.tz) || !r.readF32(e.transform.rx) ||
        !r.readF32(e.transform.ry) || !r.readF32(e.transform.rz) ||
        !r.readF32(e.transform.rw) || !r.readF32(e.transform.sx) ||
        !r.readF32(e.transform.sy) || !r.readF32(e.transform.sz)) {
      return fail("Failed to read entity transform");
    }

    if (!r.readU64(e.hierarchy.parent))
      return fail("Failed to read entity hierarchy");

    uint8_t has = 0;
    if (!r.readU8(has))
      return fail("Failed to read camera presence flag");
    e.hasCamera = has != 0;
    if (e.hasCamera) {
      if (!r.readF32(e.camera.fovY) || !r.readF32(e.camera.nearZ) ||
          !r.readF32(e.camera.farZ) || !r.readF32(e.camera.aperture) ||
          !r.readF32(e.camera.focusDistance) || !r.readF32(e.camera.sensorWidth)) {
        return fail("Failed to read camera component");
      }
      if (outScene.header.versionMinor >= 1) {
        if (!r.readF32(e.camera.sensorHeight))
          return fail("Failed to read camera sensor height");
      } else {
        e.camera.sensorHeight = 24.0f;
      }
      uint8_t act = 0;
      if (!r.readU8(act))
        return fail("Failed to read camera active flag");
      e.camera.active = act != 0;
    }

    if (!r.readU8(has))
      return fail("Failed to read light presence flag");
    e.hasLight = has != 0;
    if (e.hasLight) {
      uint8_t t = 0;
      if (!r.readU8(t) || !r.readF32(e.light.color[0]) ||
          !r.readF32(e.light.color[1]) || !r.readF32(e.light.color[2]) ||
          !r.readF32(e.light.intensity) || !r.readF32(e.light.range) ||
          !r.readF32(e.light.spotAngle)) {
        return fail("Failed to read light component");
      }
      e.light.type = (SceneLightType)t;
    }

    if (!r.readU8(has))
      return fail("Failed to read renderable presence flag");
    e.hasRenderable = has != 0;
    if (e.hasRenderable) {
      if (!r.readStringU32(e.renderable.meshAsset) ||
          !r.readStringU32(e.renderable.materialAsset)) {
        return fail("Failed to read renderable component");
      }
    }
  }

  return true;
}

} // namespace Nyx

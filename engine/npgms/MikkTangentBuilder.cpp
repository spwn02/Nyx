#include "MikkTangentBuilder.h"

#include "MeshCPU.h"

#include "core/Assert.h"
#include <algorithm>
#include <cstddef>
#include <vector>

#include "mikktspace.h"

namespace Nyx::Tangents {

struct MikkUserData final {
  MeshCPU *mesh = nullptr;
  // 3 indices per face, face -> vertex index mapping
};

static int mikk_getNumFaces(const SMikkTSpaceContext *ctx) {
  auto *ud = reinterpret_cast<MikkUserData *>(ctx->m_pUserData);
  const auto &idx = ud->mesh->indices;
  return idx.size() / 3u;
}

static int mikk_getNumVertsOfFace(const SMikkTSpaceContext *, const int) {
  return 3;
}

static void getCornerIndex(const MikkUserData *ud, int face, int vert,
                           uint32_t &outIndex) {
  const size_t base = face * 3u;
  outIndex = ud->mesh->indices[base + vert];
}

static void mikk_getPosition(const SMikkTSpaceContext *ctx, float outPos[3],
                             const int face, const int vert) {
  auto *ud = reinterpret_cast<MikkUserData *>(ctx->m_pUserData);
  uint32_t i = 0;
  getCornerIndex(ud, face, vert, i);
  const auto &v = ud->mesh->vertices[i];
  outPos[0] = v.pos.x;
  outPos[1] = v.pos.y;
  outPos[2] = v.pos.z;
}

static void mikk_getNormal(const SMikkTSpaceContext *ctx, float outNormal[3],
                           const int face, const int vert) {
  auto *ud = reinterpret_cast<MikkUserData *>(ctx->m_pUserData);
  uint32_t i = 0;
  getCornerIndex(ud, face, vert, i);
  const auto &v = ud->mesh->vertices[i];
  outNormal[0] = v.nrm.x;
  outNormal[1] = v.nrm.y;
  outNormal[2] = v.nrm.z;
}

static void mikk_getTexCoord(const SMikkTSpaceContext *ctx,
                             float outTexCoord[2], const int face,
                             const int vert) {
  auto *ud = reinterpret_cast<MikkUserData *>(ctx->m_pUserData);
  uint32_t i = 0;
  getCornerIndex(ud, face, vert, i);
  const auto &v = ud->mesh->vertices[i];
  outTexCoord[0] = v.uv.x;
  outTexCoord[1] = v.uv.y;
}

static void mikk_setTSpaceBasic(const SMikkTSpaceContext *ctx,
                                const float tangent[3], const float sign,
                                const int face, const int vert) {
  auto *ud = reinterpret_cast<MikkUserData *>(ctx->m_pUserData);
  uint32_t i = 0;
  getCornerIndex(ud, face, vert, i);

  auto &v = ud->mesh->vertices[i];
  v.tan = glm::vec4(tangent[0], tangent[1], tangent[2], sign);
}

bool buildTangents_Mikk(MeshCPU &mesh) {
  if (mesh.indices.empty() || mesh.vertices.empty())
    return false;

  if ((mesh.indices.size() % 3u) != 0u)
    return false;

  // Prereq: normals + uvs must exist.
  for (auto &v : mesh.vertices)
    v.tan = glm::vec4(1, 0, 0, 1);

  SMikkTSpaceInterface iface{};
  iface.m_getNumFaces = mikk_getNumFaces;
  iface.m_getNumVerticesOfFace = mikk_getNumVertsOfFace;
  iface.m_getPosition = mikk_getPosition;
  iface.m_getNormal = mikk_getNormal;
  iface.m_getTexCoord = mikk_getTexCoord;
  iface.m_setTSpaceBasic = mikk_setTSpaceBasic;

  MikkUserData ud;
  ud.mesh = &mesh;

  SMikkTSpaceContext ctx{};
  ctx.m_pInterface = &iface;
  ctx.m_pUserData = &ud;

  const int ok = genTangSpaceDefault(&ctx);
  return ok != 0;
}

} // namespace Nyx::Tangents

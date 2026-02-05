#pragma once

namespace Nyx {

struct MeshCPU;

namespace Tangents {

// Builds MikkTSpace tangnets into MeshCPu vertices.
// Requirements:
// - mesh.indices is triangles (3 per face)
// - msh.vertices have position, normal, uv
// Output:
// - vertex.tangent = vec4(T.xyz, sign)
// Returns false if prerequisites are missing.
bool buildTangents_Mikk(MeshCPU &m);

} // namespace Tangents

} // namespace Nyx

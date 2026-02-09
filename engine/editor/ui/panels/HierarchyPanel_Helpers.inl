static const char *meshTypeName(ProcMeshType t) {
  switch (t) {
  case ProcMeshType::Cube:
    return "Cube";
  case ProcMeshType::Plane:
    return "Plane";
  case ProcMeshType::Circle:
    return "Circle";
  case ProcMeshType::Sphere:
    return "Sphere";
  case ProcMeshType::Monkey:
    return "Monkey";
  default:
    return "Unknown";
  }
}

static uintptr_t treeId(EntityID e) {
  return (uintptr_t(e.generation) << 32) | uintptr_t(e.index);
}

// Material drag/drop

static bool beginMaterialDragSource(MaterialHandle mh, const char *label) {
  if (mh == InvalidMaterial)
    return false;
  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
    UiPayload::MaterialHandlePayload payload{mh.slot, mh.gen};
    ImGui::SetDragDropPayload(UiPayload::MaterialHandle, &payload,
                              sizeof(payload));
    ImGui::Text("Material: %s", label ? label : "(unnamed)");
    ImGui::EndDragDropSource();
    return true;
  }
  return false;
}

static bool acceptMaterialDrop(MaterialHandle &outMh) {
  if (!ImGui::BeginDragDropTarget())
    return false;
  if (const ImGuiPayload *p =
          ImGui::AcceptDragDropPayload(UiPayload::MaterialHandle)) {
    if (p->Data && p->DataSize == sizeof(UiPayload::MaterialHandlePayload)) {
      const auto *pl = (const UiPayload::MaterialHandlePayload *)p->Data;
      outMh = MaterialHandle{pl->slot, pl->gen};
      ImGui::EndDragDropTarget();
      return true;
    }
  }
  ImGui::EndDragDropTarget();
  return false;
}

static void applyMaterialToSubmesh(World &world, EntityID e, uint32_t si,
                                   MaterialHandle mh) {
  if (!world.hasMesh(e) || world.hasLight(e))
    return;
  world.submesh(e, si).material = mh;
  world.events().push({WorldEventType::MeshChanged, e});
}

static void applyMaterialToAllSubmeshes(World &world, EntityID e,
                                        MaterialHandle mh) {
  if (!world.hasMesh(e) || world.hasLight(e))
    return;
  const uint32_t n = world.submeshCount(e);
  for (uint32_t si = 0; si < n; ++si) {
    applyMaterialToSubmesh(world, e, si, mh);
  }
  world.events().push({WorldEventType::MeshChanged, e});
}

static void DrawAtlasIconAt(const Nyx::IconAtlas &atlas,
                            const Nyx::AtlasRegion &r, ImVec2 p, ImVec2 size,
                            ImU32 tint = IM_COL32(220, 220, 220, 255)) {
  p.x = std::floor(p.x + 0.5f);
  p.y = std::floor(p.y + 0.5f);
  size.x = std::floor(size.x + 0.5f);
  size.y = std::floor(size.y + 0.5f);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddImage(atlas.imguiTexId(), p, ImVec2(p.x + size.x, p.y + size.y), r.uv0,
               r.uv1, tint);
}

static uint64_t hashPreviewSettings(const EngineContext &engine) {
  const glm::vec3 &d = engine.previewLightDir();
  const glm::vec3 &c = engine.previewLightColor();
  const float i = engine.previewLightIntensity();
  const float e = engine.previewLightExposure();
  const float a = engine.previewAmbient();
  auto hf = [](float v) -> uint64_t {
    uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(u));
    return (uint64_t)u;
  };
  uint64_t h = 1469598103934665603ull;
  h ^= hf(d.x) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(d.y) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(d.z) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(c.x) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(c.y) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(c.z) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(i) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(e) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  h ^= hf(a) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  return h;
}

static void clearMaterialFromWorld(World &world, MaterialHandle h) {
  if (h == InvalidMaterial)
    return;
  for (EntityID e : world.alive()) {
    if (!world.isAlive(e) || !world.hasMesh(e))
      continue;
    const uint32_t n = world.submeshCount(e);
    for (uint32_t si = 0; si < n; ++si) {
      auto &sm = world.submesh(e, si);
      if (sm.material == h)
        sm.material = InvalidMaterial;
    }
  }
}

static void setHiddenRecursive(World &world, EntityID e, bool hidden) {
  if (!world.isAlive(e))
    return;
  auto &tr = world.transform(e);
  tr.hidden = hidden;
  EntityID ch = world.hierarchy(e).firstChild;
  while (ch != InvalidEntity) {
    EntityID next = world.hierarchy(ch).nextSibling;
    setHiddenRecursive(world, ch, hidden);
    ch = next;
  }
}

static void isolateEntity(World &world, EntityID e, EntityID keepVisible) {
  for (EntityID id : world.alive()) {
    if (!world.isAlive(id))
      continue;
    world.transform(id).hidden = true;
  }
  if (keepVisible != InvalidEntity && world.isAlive(keepVisible))
    world.transform(keepVisible).hidden = false;
  setHiddenRecursive(world, e, false);
}

static void unisolateAll(World &world, EntityID keepVisible) {
  for (EntityID id : world.alive()) {
    if (!world.isAlive(id))
      continue;
    world.transform(id).hidden = false;
  }
  if (keepVisible != InvalidEntity && world.isAlive(keepVisible))
    world.transform(keepVisible).hidden = false;
}

static void resetTransform(World &world, EntityID e) {
  if (!world.isAlive(e))
    return;
  auto &t = world.transform(e);
  t.translation = glm::vec3(0.0f);
  t.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  t.scale = glm::vec3(1.0f);
  t.dirty = true;
  world.worldTransform(e).dirty = true;
}

static void resetTransformRecursive(World &world, EntityID e) {
  resetTransform(world, e);
  EntityID ch = world.hierarchy(e).firstChild;
  while (ch != InvalidEntity) {
    EntityID next = world.hierarchy(ch).nextSibling;
    resetTransformRecursive(world, ch);
    ch = next;
  }
}

static void selectEntities(World &world, Selection &sel,
                           const std::vector<EntityID> &ents) {
  sel.kind = SelectionKind::Picks;
  sel.picks.clear();
  sel.pickEntity.clear();
  for (EntityID e : ents) {
    if (!world.isAlive(e))
      continue;
    const uint32_t pid = packPick(e, 0);
    sel.picks.push_back(pid);
    sel.pickEntity.emplace(pid, e);
  }
  if (!sel.picks.empty()) {
    sel.activePick = sel.picks.back();
    sel.activeEntity = sel.entityForPick(sel.activePick);
  } else {
    sel.clear();
  }
}

static void gatherEntityPicks(World &world, EntityID e,
                              std::vector<uint32_t> &out) {
  if (!world.isAlive(e))
    return;

  if (!world.hasMesh(e)) {
    // still allow selecting entity even without mesh: represent it as submesh 0
    // (you can also choose to skip this entirely)
    out.push_back(packPick(e, 0));
    return;
  }

  const uint32_t n = world.submeshCount(e);
  if (n == 0) {
    out.push_back(packPick(e, 0));
    return;
  }

  out.reserve(out.size() + n);
  for (uint32_t si = 0; si < n; ++si)
    out.push_back(packPick(e, si));
}

static void setSingleEntity(World &world, Selection &sel, EntityID e) {
  std::vector<uint32_t> tmp;
  gatherEntityPicks(world, e, tmp);
  if (tmp.empty()) {
    sel.clear();
    return;
  }
  sel.kind = SelectionKind::Picks;
  sel.picks = tmp;
  sel.activePick = tmp.front();
  sel.activeEntity = e;
  sel.pickEntity.clear();
  for (uint32_t p : tmp)
    sel.pickEntity.emplace(p, e);
}

static void addEntity(World &world, Selection &sel, EntityID e) {
  std::vector<uint32_t> tmp;
  gatherEntityPicks(world, e, tmp);
  if (tmp.empty())
    return;

  if (sel.kind != SelectionKind::Picks) {
    sel.kind = SelectionKind::Picks;
    sel.picks.clear();
    sel.pickEntity.clear();
  }

  for (uint32_t p : tmp) {
    if (!sel.hasPick(p))
      sel.picks.push_back(p);
  }
  sel.activePick = tmp.front();
  sel.activeEntity = e;
  for (uint32_t p : tmp)
    sel.pickEntity.emplace(p, e);
}

static void toggleEntity(World &world, Selection &sel, EntityID e) {
  std::vector<uint32_t> tmp;
  gatherEntityPicks(world, e, tmp);
  if (tmp.empty())
    return;

  if (sel.kind != SelectionKind::Picks) {
    // toggle on => single-entity
    setSingleEntity(world, sel, e);
    return;
  }

  // If ALL picks are present => remove them. Else => add missing.
  bool allPresent = true;
  for (uint32_t p : tmp) {
    if (!sel.hasPick(p)) {
      allPresent = false;
      break;
    }
  }

  if (allPresent) {
    // remove all of tmp from sel.picks
    auto &v = sel.picks;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](uint32_t x) {
                             for (uint32_t p : tmp)
                               if (p == x)
                                 return true;
                             return false;
                           }),
            v.end());
    for (uint32_t p : tmp)
      sel.pickEntity.erase(p);

    if (v.empty()) {
      sel.clear();
    } else {
      sel.activePick = v.back();
      sel.activeEntity = sel.entityForPick(sel.activePick);
    }
  } else {
    for (uint32_t p : tmp) {
      if (!sel.hasPick(p))
        sel.picks.push_back(p);
    }
    sel.activePick = tmp.front();
    sel.activeEntity = e;
    for (uint32_t p : tmp)
      sel.pickEntity.emplace(p, e);
  }
}

static void rangeSelectEntities(World &world, Selection &sel,
                                const std::vector<EntityID> &order, EntityID a,
                                EntityID b) {
  if (a == InvalidEntity || b == InvalidEntity) {
    setSingleEntity(world, sel, b);
    return;
  }

  auto ia = std::find(order.begin(), order.end(), a);
  auto ib = std::find(order.begin(), order.end(), b);
  if (ia == order.end() || ib == order.end()) {
    setSingleEntity(world, sel, b);
    return;
  }
  if (ia > ib)
    std::swap(ia, ib);

  sel.kind = SelectionKind::Picks;
  sel.picks.clear();
  sel.pickEntity.clear();

  for (auto it = ia; it != std::next(ib); ++it) {
    std::vector<uint32_t> tmp;
    gatherEntityPicks(world, *it, tmp);
    for (uint32_t p : tmp) {
      sel.picks.push_back(p);
      sel.pickEntity.emplace(p, *it);
    }
  }

  if (!sel.picks.empty()) {
    sel.activePick = packPick(b, 0);
    sel.activeEntity = b;
    sel.pickEntity.emplace(sel.activePick, b);
  } else {
    sel.clear();
  }
}

static bool isEntityHighlightedByPicks(const Selection &sel, EntityID e,
                                       uint32_t subCount) {
  if (sel.kind != SelectionKind::Picks || sel.picks.empty())
    return false;

  for (uint32_t si = 0; si < std::max(1u, subCount); ++si) {
    const uint32_t p = packPick(e, si);
    if (sel.hasPick(p))
      return true;
  }
  return false;
}

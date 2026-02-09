#include "EditorHistory.h"

#include "scene/JsonLite.h"

#include <filesystem>
#include <fstream>

namespace Nyx {

#include "EditorHistory_PersistenceHelpers.inl"

bool EditorHistory::saveToFile(const std::string &path) const {
  Object root;
  root["type"] = "NyxHistory";
  root["version"] = 1;
  root["cursor"] = (double)m_cursor;
  root["nextId"] = (double)m_nextId;
  root["maxEntries"] = (double)m_maxEntries;
  Array ents;
  ents.reserve(m_entries.size());
  for (const auto &e : m_entries) {
    Object je;
    je["id"] = (double)e.id;
    je["label"] = e.label;
    je["time"] = e.timestampSec;
    je["selection"] = jSelection(e.selection);
    Array ops;
    ops.reserve(e.ops.size());
    for (const auto &op : e.ops)
      ops.emplace_back(jHistoryOp(op));
    je["ops"] = Value(std::move(ops));
    ents.emplace_back(Value(std::move(je)));
  }
  root["entries"] = Value(std::move(ents));
  const std::string out = stringify(Value(std::move(root)), true, 2);
  std::filesystem::path p(path);
  std::error_code ec;
  std::filesystem::create_directories(p.parent_path(), ec);
  std::ofstream f(path, std::ios::binary);
  if (!f.is_open())
    return false;
  f.write(out.data(), (std::streamsize)out.size());
  return true;
}

bool EditorHistory::loadFromFile(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open())
    return false;
  std::string text((std::istreambuf_iterator<char>(f)),
                   std::istreambuf_iterator<char>());
  Value root;
  JsonLite::ParseError err{};
  if (!parse(text, root, err))
    return false;
  if (!root.isObject())
    return false;
  if (const Value *vt = root.get("type"); !vt || !vt->isString() ||
                                           vt->asString() != "NyxHistory")
    return false;
  if (const Value *vc = root.get("cursor"); vc && vc->isNum())
    m_cursor = (int)vc->asNum(m_cursor);
  if (const Value *vn = root.get("nextId"); vn && vn->isNum())
    m_nextId = (uint64_t)vn->asNum(m_nextId);
  if (const Value *vm = root.get("maxEntries"); vm && vm->isNum())
    m_maxEntries = (size_t)vm->asNum(m_maxEntries);
  if (const Value *ve = root.get("entries"); ve && ve->isArray()) {
    m_entries.clear();
    for (const Value &it : ve->asArray()) {
      if (!it.isObject())
        continue;
      HistoryEntry e{};
      if (const Value *vid = it.get("id"); vid && vid->isNum())
        e.id = (uint64_t)vid->asNum();
      if (const Value *vl = it.get("label"); vl && vl->isString())
        e.label = vl->asString();
      if (const Value *vt = it.get("time"); vt && vt->isNum())
        e.timestampSec = vt->asNum();
      if (const Value *vs = it.get("selection"))
        readSelection(*vs, e.selection);
      if (const Value *vo = it.get("ops"); vo && vo->isArray()) {
        for (const Value &opv : vo->asArray()) {
          HistoryOp op;
          if (readHistoryOp(opv, op))
            e.ops.emplace_back(std::move(op));
        }
      }
      m_entries.push_back(std::move(e));
    }
  }
  if (m_entries.size() > m_maxEntries) {
    const size_t toDrop = m_entries.size() - m_maxEntries;
    m_entries.erase(m_entries.begin(), m_entries.begin() + (ptrdiff_t)toDrop);
    m_cursor = std::max(-1, m_cursor - (int)toDrop);
  }
  ++m_revision;
  return true;
}


} // namespace Nyx

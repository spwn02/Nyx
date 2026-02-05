#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Nyx {

// NOTE: Bloom is NOT a post-filter node in this system (separate block).
// This registry is only for post-filters compiled into FilterStack SSBO.

using FilterTypeId = uint32_t;

// UI hint for node parameters (Editor can decide widget type).
enum class FilterParamUI : uint8_t {
  Slider = 0,
  Drag,
  Checkbox, // float 0/1
  Color3,   // 3 floats
  Color4,   // 4 floats
};

struct FilterParamDesc final {
  const char *name = "";
  float defaultValue = 0.0f;
  float minValue = 0.0f;
  float maxValue = 1.0f;
  float step = 0.01f;
  FilterParamUI ui = FilterParamUI::Slider;
};

// A filter "type" (like Contrast, Saturation, Vignette) registered once.
struct FilterTypeInfo final {
  FilterTypeId id = 0;

  const char *name = "";         // display name
  const char *category = "";     // grouping in AddMenu (Color/Lens/Tone/Utility/etc.)
  const char *defaultLabel = ""; // default node label in graph

  // Optional search aliases (e.g. "EV" for Exposure)
  std::vector<std::string> aliases;

  // Derived keywords (filled automatically)
  std::vector<std::string> keywords;

  // Parameters exposed by UI/editor; stored as float array per node instance.
  static constexpr uint32_t kMaxParams = 16;
  uint32_t paramCount = 0;
  FilterParamDesc params[kMaxParams]{};

  // How many floats this filter packs into GPU SSBO (can be <= paramCount).
  // (For now we keep it equal to paramCount for simplicity; still useful metadata.)
  uint32_t gpuParamCount = 0;
};

// A filter node instance placed by the user in the chain.
struct FilterNode final {
  static constexpr uint32_t kMaxParams = FilterTypeInfo::kMaxParams;

  FilterTypeId type = 0;
  bool enabled = true;

  // Editor-facing label (can be renamed by user). Default comes from type.
  std::string label;

  // Parameter payload for this node instance.
  // Interpretation is type-specific (via registry param descriptors).
  float params[kMaxParams]{};
};

// Central registry of filter types.
class FilterRegistry final {
public:
  FilterRegistry();

  // Removes all types (usually only used for tests/tools).
  void clear();

  // Builtin registrations
  void registerBuiltins();

  // Rebuild lookup maps after registrations.
  void finalize();

  // Access
  const std::vector<FilterTypeInfo> &types() const;
  const FilterTypeInfo *find(FilterTypeId id) const;
  const FilterTypeInfo *findByName(std::string_view name) const;

  // Search for AddMenu: query over name/category/aliases/params.
  // Optional category filter: pass "" to ignore.
  std::vector<const FilterTypeInfo *> search(
      std::string_view query, std::string_view category = {}) const;

  // Categories list for UI grouping
  std::vector<std::string> categories() const;

  // Create node instance with defaults
  FilterNode makeNode(FilterTypeId id) const;
  void resetToDefaults(FilterNode &node) const;

  // Convenience
  uint32_t maxGpuParamCount() const;

private:
  std::vector<FilterTypeInfo> m_types;

  // Lookups (pointers remain valid because m_types is stable after finalize()).
  std::unordered_map<FilterTypeId, const FilterTypeInfo *> m_byId;
  std::unordered_map<std::string, const FilterTypeInfo *> m_byName;       // exact case
  std::unordered_map<std::string, const FilterTypeInfo *> m_byLowerName;  // lowercased + aliases
};

} // namespace Nyx

#pragma once

#include "scene/material/MaterialTypes.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx {

enum class TexColorSpace : uint8_t { Linear = 0, SRGB };

struct SlotPolicy final {
  MaterialTexSlot slot{};
  const char *label = "";
  TexColorSpace requiredSpace = TexColorSpace::Linear;
  const char *hint = "";
  const char *preferredExtCSV = "";
};

inline const SlotPolicy &materialSlotPolicy(MaterialTexSlot s) {
  static const SlotPolicy k[(size_t)MaterialTexSlot::Count] = {
      {MaterialTexSlot::BaseColor, "Base Color", TexColorSpace::SRGB,
       "sRGB color (albedo/baseColor).", "png,jpg,jpeg,tga,bmp,ktx,ktx2"},
      {MaterialTexSlot::Emissive, "Emissive", TexColorSpace::SRGB,
       "sRGB color (emissive).", "png,jpg,jpeg,tga,bmp,ktx,ktx2"},
      {MaterialTexSlot::Normal, "Normal", TexColorSpace::Linear,
       "Linear (normal map).", "png,tga,bmp,ktx,ktx2"},
      {MaterialTexSlot::Metallic, "Metallic", TexColorSpace::Linear,
       "Linear (metallic).", "png,tga,bmp,ktx,ktx2"},
      {MaterialTexSlot::Roughness, "Roughness", TexColorSpace::Linear,
       "Linear (roughness).", "png,tga,bmp,ktx,ktx2"},
      {MaterialTexSlot::AO, "AO", TexColorSpace::Linear,
       "Linear (occlusion).", "png,tga,bmp,ktx,ktx2"},
  };
  return k[(size_t)s];
}

struct SlotBinding final {
  uint32_t texIndex = 0xFFFFFFFF; // TextureTable::Invalid
  std::string path;
  bool requestedSRGB = false;
};

enum class SlotIssueKind : uint8_t {
  None = 0,
  MissingFileExtension,
  UnsupportedExtension,
  WrongColorSpace,
  EmptyPath,
};

struct SlotIssue final {
  SlotIssueKind kind = SlotIssueKind::None;
  std::string message;
};

inline std::string toLower(std::string s) {
  for (char &c : s) {
    if (c >= 'A' && c <= 'Z')
      c = (char)(c - 'A' + 'a');
  }
  return s;
}

inline std::string fileExtLower(std::string_view path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string_view::npos || dot + 1 >= path.size())
    return {};
  std::string ext(path.substr(dot + 1));
  return toLower(std::move(ext));
}

inline bool csvContainsExt(std::string_view csvLower,
                           std::string_view extLower) {
  if (extLower.empty())
    return false;
  size_t start = 0;
  while (start < csvLower.size()) {
    size_t comma = csvLower.find(',', start);
    if (comma == std::string_view::npos)
      comma = csvLower.size();
    const auto token = csvLower.substr(start, comma - start);
    if (token == extLower)
      return true;
    start = comma + 1;
  }
  return false;
}

inline bool isSRGBRequired(MaterialTexSlot s) {
  return materialSlotPolicy(s).requiredSpace == TexColorSpace::SRGB;
}

inline SlotIssue validateSlot(MaterialTexSlot slot, const SlotBinding &b) {
  const auto &p = materialSlotPolicy(slot);

  if (b.path.empty()) {
    if (b.texIndex != 0xFFFFFFFF) {
      return {SlotIssueKind::EmptyPath,
              std::string(p.label) +
                  ": texture index set but path is empty."};
    }
    return {};
  }

  const std::string ext = fileExtLower(b.path);
  if (ext.empty()) {
    return {SlotIssueKind::MissingFileExtension,
            std::string(p.label) + ": file has no extension."};
  }

  const std::string pref = toLower(std::string(p.preferredExtCSV));
  if (!pref.empty() && !csvContainsExt(pref, ext)) {
    return {SlotIssueKind::UnsupportedExtension,
            std::string(p.label) + ": unusual extension '." + ext +
                "'. Preferred: " + p.preferredExtCSV};
  }

  const bool requiredSRGB = (p.requiredSpace == TexColorSpace::SRGB);
  if (b.requestedSRGB != requiredSRGB) {
    return {SlotIssueKind::WrongColorSpace,
            std::string(p.label) + ": wrong color space. Expected " +
                (requiredSRGB ? "sRGB" : "Linear") +
                " but texture was loaded as " +
                (b.requestedSRGB ? "sRGB" : "Linear") + "."};
  }

  return {};
}

inline std::vector<SlotIssue> validateAll(
    const SlotBinding slots[(size_t)MaterialTexSlot::Count]) {
  std::vector<SlotIssue> out;
  for (size_t i = 0; i < (size_t)MaterialTexSlot::Count; ++i) {
    SlotIssue iss = validateSlot((MaterialTexSlot)i, slots[i]);
    if (iss.kind != SlotIssueKind::None)
      out.push_back(std::move(iss));
  }
  return out;
}

} // namespace Nyx

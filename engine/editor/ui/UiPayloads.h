#pragma once

#include <cstdint>

namespace Nyx::UiPayload {

static constexpr const char *TexturePath = "NYX_TEX_PATH";
static constexpr const char *MaterialHandle = "NYX_MAT_HANDLE";
static constexpr const char *AssetId = "NYX_ASSET_ID";
static constexpr const char *AssetPath = "NYX_ASSET_PATH";
static constexpr const char *AssetRelPath = "NYX_ASSET_REL_PATH";

struct MaterialHandlePayload {
  uint32_t slot = 0;
  uint32_t gen = 0;
};

} // namespace Nyx::UiPayload

#pragma once

#include <optional>
#include <string>

namespace Nyx::FileDialogs {

// Native OS file pickr (Explorer/Finder/etc).
// Returns absolute path on success, std::nullopt on cancel/failure.
//
// filterList example: "png,jpg,jpeg,tga,bmp,ktx,ktx2,hdr,exr"
std::optional<std::string> openFile(const char *title, const char *filterList,
                                    const char *defaultPath = nullptr);

// Native save dialog.
// Returns absolute path on success, std::nullopt on cancel/failure.
std::optional<std::string> saveFile(const char *title, const char *filterList,
                                    const char *defaultPath = nullptr);

} // namespace Nyx::FileDialogs

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx::FileUtil {

bool readFileBytes(const std::string &path, std::vector<uint8_t> &out);

bool writeFileBytesAtomic(const std::string &path, const void *data, size_t size,
                          std::string *outError = nullptr);

std::string directoryOf(const std::string &path);
std::string filenameOf(const std::string &path);
std::string joinPath(const std::string &a, const std::string &b);

} // namespace Nyx::FileUtil

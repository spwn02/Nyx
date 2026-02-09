#include "FileUtil.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#endif

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace Nyx::FileUtil {

static bool writeAll(FILE *f, const void *data, size_t size) {
  const uint8_t *p = (const uint8_t *)data;
  size_t left = size;
  while (left > 0) {
    const size_t n = std::fwrite(p, 1, left, f);
    if (n == 0)
      return false;
    p += n;
    left -= n;
  }
  return true;
}

bool readFileBytes(const std::string &path, std::vector<uint8_t> &out) {
  out.clear();
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return false;

  std::fseek(f, 0, SEEK_END);
  const long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);

  if (sz < 0) {
    std::fclose(f);
    return false;
  }

  out.resize((size_t)sz);
  if (sz > 0) {
    const size_t read = std::fread(out.data(), 1, (size_t)sz, f);
    std::fclose(f);
    return read == (size_t)sz;
  }

  std::fclose(f);
  return true;
}

std::string directoryOf(const std::string &path) {
#if __has_include(<filesystem>)
  fs::path p(path);
  return p.parent_path().string();
#else
  const size_t pos = path.find_last_of("/\\");
  return (pos == std::string::npos) ? std::string() : path.substr(0, pos);
#endif
}

std::string filenameOf(const std::string &path) {
#if __has_include(<filesystem>)
  fs::path p(path);
  return p.filename().string();
#else
  const size_t pos = path.find_last_of("/\\");
  return (pos == std::string::npos) ? path : path.substr(pos + 1);
#endif
}

std::string joinPath(const std::string &a, const std::string &b) {
#if __has_include(<filesystem>)
  fs::path p = fs::path(a) / fs::path(b);
  return p.string();
#else
  if (a.empty())
    return b;
  if (b.empty())
    return a;
  const char last = a.back();
  if (last == '/' || last == '\\')
    return a + b;
  return a + "/" + b;
#endif
}

bool writeFileBytesAtomic(const std::string &path, const void *data, size_t size,
                          std::string *outError) {
  if (outError)
    outError->clear();

  const std::string dir = directoryOf(path);
  const std::string file = filenameOf(path);

  std::string tmp = file + ".tmp";
  tmp += ".";
  tmp += std::to_string((uint64_t)std::hash<std::string>{}(path));
  const std::string tmpPath = dir.empty() ? tmp : joinPath(dir, tmp);

  FILE *f = std::fopen(tmpPath.c_str(), "wb");
  if (!f) {
    if (outError)
      *outError = "Failed to open temp file for write: " + tmpPath;
    return false;
  }

  bool ok = writeAll(f, data, size);
  ok = ok && (std::fflush(f) == 0);
#if defined(_WIN32)
#else
  const int fd = fileno(f);
  if (fd >= 0)
    (void)::fsync(fd);
#endif
  std::fclose(f);

  if (!ok) {
    std::remove(tmpPath.c_str());
    if (outError)
      *outError = "Failed to write all bytes to temp file: " + tmpPath;
    return false;
  }

#if __has_include(<filesystem>)
  std::error_code ec;
  fs::rename(tmpPath, path, ec);
  if (ec) {
    std::error_code ec2;
    fs::remove(path, ec2);
    fs::rename(tmpPath, path, ec2);
    if (ec2) {
      if (outError)
        *outError = "Failed to rename temp->target: " + ec2.message();
      fs::remove(tmpPath, ec2);
      return false;
    }
  }
  return true;
#else
  std::remove(path.c_str());
  if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
    if (outError)
      *outError = std::string("rename() failed");
    std::remove(tmpPath.c_str());
    return false;
  }
  return true;
#endif
}

} // namespace Nyx::FileUtil

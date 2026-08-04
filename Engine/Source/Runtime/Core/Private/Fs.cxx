module Nyx.Core;

import std;
import :Fs;

namespace Nyx::fs {

auto File::read() const -> Result<String> {
  if (not ok())
    return bail({
        "Failed to open a file: {}",
        path_.string(),
    });

  std::stringstream buf;

  buf << file_.rdbuf();
  return buf.str();
}

auto File::write(StringView buf) -> Result<void> {
  if (not ok())
    return bail({
        "Failed to open a file: {}",
        path_.string(),
    });

  file_ << buf;

  return {};
}

auto OpenOptions::open(const Path &path) const -> Result<File> {
  if (std::filesystem::is_directory(path)) {
    return bail({
        "Provided file is a directory: {}",
        path.string(),
    });
  }

  using ios = std::ios;
  ios::openmode mode{};

  auto create_check = [&]() -> Result<void> {
    if (not(mode & ios::out))
      return bail({
          "Attempted to create a file without write/append mode: {}",
          path.string(),
      });

    std::error_code err;
    std::filesystem::create_directories(path.parent_path(), err);
    if (err) {
      String msg = err.message();
      return bail({
          "Failed to create parent directory: {}",
          msg,
      });
    }

    return {};
  };

  if (read)
    mode |= ios::in;
  if (write)
    mode |= ios::out;
  if (append)
    mode |= ios::app | ios::out;
  if (truncate) {
    if ((mode & ios::out) == 0)
      return bail({
          "Attempted to truncate file without write mode: {}",
          path.string(),
      });
    mode |= ios::trunc;
  }
  if (create) {
    Result<void> res = create_check();
    if (not res)
      return bail(std::move(res.error()));
  } else if (create_new) {
    if (std::filesystem::exists(path))
      return bail({"File already exists: {}", path.string()});

    Result<void> res = create_check();
    if (not res)
      return bail(std::move(res.error()));
  }

  if ((mode & (ios::in | ios::out)) == 0)
    return bail({
        "Attempted to open a file without any of read/write/append modes: {}",
        path.string(),
    });

  if (not(create or create_new) and not std::filesystem::exists(path))
    return bail({
        "Attempted to access a non-existent file without create/create_new "
        "mode: {}",
        path.string(),
    });

  File file{path, mode};
  if (!file)
    return bail({"Failed to open a file: {}", path.string()});

  return file;
}

} // namespace Nyx::fs

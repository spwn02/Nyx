export module Nyx.Core:Fs;

import std;
import :Types;
import :Error;

export namespace Nyx::fs {

class File {
public:
  explicit File(const Path &path, const std::ios::openmode &openmode) noexcept
      : file_(path, openmode), path_(path) {}
  ~File() { close(); }

  File(const File &) = delete (
      "Since this class manages unique file streams, copy is not "
      "supported; use move instead");
  File(File &&) noexcept = default;
  auto operator=(const File &) -> File & = delete (
      "Since this class manages unique file streams, copy is not "
      "supported; use move instead");
  auto operator=(File &&) noexcept -> File & = default;

  auto read() const -> Result<String>;
  auto write(StringView buf) -> Result<void>;

  constexpr auto close() noexcept -> void {
    if (ok()) {
      file_.close();
    }
  }
  constexpr auto delete_file() noexcept -> Result<void> {
    if (ok()) {
      file_.close();
      std::error_code err;
      std::filesystem::remove(path_, err);
      if (err) {
        String msg = err.message();
        return bail({
            "Failed to delete file: {}: {}",
             path_.string(),
            msg,
        });
      }
    }
    return {};
  }
  constexpr auto ok() const noexcept -> bool { return file_.is_open(); }

  explicit constexpr operator bool() const { return file_.is_open(); }

private:
  std::fstream file_;
  Path path_;
};

struct OpenOptions {
  bool create{}, create_new{}, read{}, write{}, append{}, truncate{};

  [[nodiscard]]
  auto open(const Path &path) const -> Result<File>;
};

} // namespace Nyx::fs

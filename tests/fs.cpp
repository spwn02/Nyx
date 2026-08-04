#include <catch2/catch_test_macros.hpp>

import std;
import Nyx.Core;

using namespace Nyx;
using namespace Nyx::fs;

TEST_CASE("File is a directory") {
  const Path path = std::filesystem::current_path();
  constexpr OpenOptions options{
      .create = true,
  };
  const Result<File> res = options.open(path);
  REQUIRE_FALSE(res);
  const Error &err = res.error();
  REQUIRE(err.display().starts_with("Error: Provided file is a directory"));
}

TEST_CASE("Create file without write mode") {
  const Path path = std::filesystem::current_path() / "tests" / "foo.txt";
  constexpr OpenOptions options{
      .create = true,
  };
  const Result<File> res = options.open(path);
  REQUIRE_FALSE(res);
  const Error &err = res.error();
  REQUIRE(err.display().starts_with("Error: Attempted to create a file without write/append mode"));
}

TEST_CASE("File doesn't exist") {
  const Path path = std::filesystem::current_path() / "tests" / "bar.txt";
  constexpr OpenOptions options{
      .read = true,
  };
  const Result<File> res = options.open(path);
  REQUIRE_FALSE(res);
  const Error &err = res.error();
  REQUIRE(err.display().starts_with("Error: Attempted to access a non-existent file "
                                    "without create/create_new mode"));
}

TEST_CASE("Read/write not chosen") {
  const Path path = std::filesystem::current_path() / "tests" / "foo.txt";
  constexpr OpenOptions options{};
  const Result<File> res = options.open(path);
  REQUIRE_FALSE(res);
  const Error &err = res.error();
  REQUIRE(err.display().starts_with("Error: Attempted to open a file without "
                                    "any of read/write/append modes"));
}

TEST_CASE("Truncate file without write mode") {
  const Path path = std::filesystem::current_path() / "tests" / "foo.txt";
  constexpr OpenOptions options{.truncate = true};
  const Result<File> res = options.open(path);
  REQUIRE_FALSE(res);
  const Error &err = res.error();
  REQUIRE(err.display().starts_with("Error: Attempted to truncate file without write mode"));
}

TEST_CASE("File operations") {
  const Path path = std::filesystem::current_path() / "tests" / "foo.txt";
  OpenOptions options{
      .create = true,
      .write = true,
  };

  /// Write file
  {
    Result<void> res = options.open(path).and_then(
        [](File file) constexpr noexcept -> Result<void> { return file.write("Hello, World!\n"); });
    REQUIRE(res);
  }

  /// File already exists
  {
    options = {
        .create_new = true,
        .write = true,
    };
    const Result<File> res = options.open(path);
    REQUIRE_FALSE(res);
    const Error &err = res.error();
    REQUIRE(err.display().starts_with("Error: File already exists"));
  }

  /// Append file
  {
    options = {
        .write = true,
        .append = true,
    };
    Result<void> res = options.open(path).and_then([](File file) constexpr noexcept -> Result<void> {
      REQUIRE(file.write("Appended 1!\n"));
      REQUIRE(file.write("Appended 2!\n"));
      return {};
    });
    REQUIRE(res);
  }

  /// Read file
  {
    options = {
        .read = true,
    };
    const Result<String> res = options.open(path).and_then(
        [](File file) constexpr noexcept -> Result<String> { return file.read(); });
    REQUIRE(res);
    REQUIRE(*res == R"(Hello, World!
Appended 1!
Appended 2!
)");
  }

  /// Truncate file
  {
    options = {
        .read = true,
        .write = true,
        .truncate = true,
    };
    const Result<String> res = options.open(path).and_then(
        [](File file) constexpr noexcept -> Result<String> { return file.read(); });
    REQUIRE(res);
    REQUIRE(*res == String{});
  }

  /// Delete file
  {
    options = {
        .write = true,
    };
    Result<void> res = options.open(path).and_then(
        [](File file) constexpr noexcept -> Result<void> { return file.delete_file(); });
    REQUIRE(res);
  }
}

#include <catch2/catch_test_macros.hpp>

import std;
import Nyx.Core;

using namespace Nyx;

auto fail() -> Result<usize> {
  return bail({"Failed"});
}

TEST_CASE("Error stores message") {
  Error err{"boom"};
  REQUIRE(err == "Error: boom");
}

TEST_CASE("Error function") {
  REQUIRE_FALSE(fail());
}

auto empty() -> Result<void> {
  return bail({});
}

auto readFile(const Path &path) -> Result<void> {
  return bail({"Failed to open a file: {}", path.string()});
}

auto compileShaders(const String &shader) -> Result<void> {
  return readFile((std::filesystem::current_path() / (shader + ".txt")))
      .transform_error([&](Error err) constexpr noexcept -> Error {
        err << std::format("Failed to compile shader: {}", shader);
        return err;
      });
}

auto initGraphics() -> Result<void> {
  return compileShaders("foo").transform_error([](Error err) constexpr noexcept -> Error {
    err << "Failed to initialize graphics";
    return err;
  });
}

auto start() -> Result<void> {
  return initGraphics().transform_error([](Error err) constexpr noexcept -> Error {
    err << "Failed to start the application";
    return err;
  });
}

TEST_CASE("Stack trace") {
  Result<void> res = start();
  REQUIRE_FALSE(res);
  REQUIRE(res.error() == R"(Error: Failed to start the application

Caused by:
  0: Failed to initialize graphics
  1: Failed to compile shader: foo
  2: Failed to open a file: /home/spawn/dev/cpp/Nyx/foo.txt)");
}

TEST_CASE("Error location") {
  Error::Message msg{"Message"};
  REQUIRE(String{msg.location.file_name()}.ends_with("Nyx/tests/error.cpp"));
}

TEST_CASE("Release error (move)") {
  Error err1{"This value is about to be moved"};
  Error err2 = err1.release();
  REQUIRE_FALSE(err1);
  REQUIRE(err2);
  err1 << "This is a new value!";
  REQUIRE(err1);

  REQUIRE(err1 == "Error: This is a new value!");
  REQUIRE(err2 == "Error: This value is about to be moved");
}

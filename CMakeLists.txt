cmake_minimum_required(VERSION 4.4 FATAL_ERROR)

# CMake 4.4 still gates native `import std;` support behind an experimental
# UUID. This must be enabled before project() discovers the C++ toolchain.
if(CMAKE_VERSION VERSION_LESS 4.5 AND NOT DEFINED
                                      CMAKE_EXPERIMENTAL_CXX_IMPORT_STD)
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
endif()

# Project
project(
  Nyx
  VERSION 0.1.0
  DESCRIPTION "A powerful 3D graphics engine."
  LANGUAGES CXX)

if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
  message(
    FATAL_ERROR
      "Nyx currently requires Ninja because the unified Miracle/Switch/Nyx "
      "build uses CMake's native C++26 'import std;' support.")
endif()

if(NOT 26 IN_LIST CMAKE_CXX_COMPILER_IMPORT_STD)
  message(FATAL_ERROR "Nyx requires toolchain support for C++26 'import std;'.")
endif()

# Compiler
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

include(build)
nyx_initialize_build_configuration()

include(compiler)

# Source
include(module)

nyx_initialize_module_system()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/Engine/Source/Runtime")

# Testing
if(NYX_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

nyx_verify_exception_boundary(ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Packaging
include(package)

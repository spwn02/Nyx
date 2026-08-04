cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

# Project
project(
  Nyx
  VERSION 0.1.0
  DESCRIPTION "A powerful 3D graphics engine."
  LANGUAGES CXX
)

# Compiler
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

include(compiler)

# Source
include(module)

nyx_initialize_module_system()
include(stdlib)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/Engine/Source/Runtime")

# Testing
enable_testing()
include(CTest)
add_subdirectory(tests)

# CPack
set(CPACK_PACKAGE_NAME "NyxEngine")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_SOURCE_IGNORE_FILES "build")
include(CPack)

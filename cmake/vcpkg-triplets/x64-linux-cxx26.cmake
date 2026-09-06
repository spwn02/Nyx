set(VCPKG_TARGET_ARCHITECTURE x64)

set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# vcpkg configures target ports in separate CMake projects. Reuse the exact
# external C++26 toolchain selected for Nyx instead of reconstructing compiler,
# libc++, reflection, contracts, module, or linker settings in this triplet.
set(_nyx_cxx26_toolchain "$ENV{CXX26_CMAKE_TOOLCHAIN_FILE}")

if(_nyx_cxx26_toolchain STREQUAL "")
  message(
    FATAL_ERROR
      "x64-linux-cxx26 requires CXX26_CMAKE_TOOLCHAIN_FILE. "
      "Source the selected reference toolchain's activate.sh before configuring Nyx.")
endif()

if(NOT IS_ABSOLUTE "${_nyx_cxx26_toolchain}"
   OR NOT EXISTS "${_nyx_cxx26_toolchain}")
  message(
    FATAL_ERROR
      "CXX26_CMAKE_TOOLCHAIN_FILE must name an existing absolute toolchain file: "
      "'${_nyx_cxx26_toolchain}'")
endif()

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${_nyx_cxx26_toolchain}")
unset(_nyx_cxx26_toolchain)

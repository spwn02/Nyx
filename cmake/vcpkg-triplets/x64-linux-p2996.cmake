set(VCPKG_TARGET_ARCHITECTURE x64)

set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../toolchains/clang-p2996.cmake")

set(NYX_LIBCXX_ROOT "$ENV{NYX_LIBCXX_ROOT}")
if(NYX_LIBCXX_ROOT STREQUAL "")
  set(NYX_LIBCXX_ROOT "$ENV{HOME}/.local/opt/clang-p2996")
endif()

set(VCPKG_CXX_FLAGS
    "-nostdinc++;-isystem${NYX_LIBCXX_ROOT}/include/x86_64-unknown-linux-gnu/c++/v1;-isystem${NYX_LIBCXX_ROOT}/include/c++/v1")
set(VCPKG_C_FLAGS "")
set(VCPKG_LINKER_FLAGS
    "-stdlib=libc++;-L${NYX_LIBCXX_ROOT}/lib/x86_64-unknown-linux-gnu")

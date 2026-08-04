# Allow overriding the installation location:
#
# CLANG_P2996_ROOT=/custom/location cmake --preset debug

if (DEFINED ENV{CLANG_P2996_ROOT} AND NOT "$ENV{CLANG_P2996_ROOT}" STREQUAL "")
  set(CLANG_P2996_ROOT "$ENV{CLANG_P2996_ROOT}")
else()
  set(CLANG_P2996_ROOT "$ENV{HOME}/.local/opt/clang-p2996")
endif()

set(P2996_CLANG "${CLANG_P2996_ROOT}/bin/clang")
set(P2996_CLANGXX "${CLANG_P2996_ROOT}/bin/clang++")

if(NOT EXISTS "${P2996_CLANG}")
  message(FATAL_ERROR
    "Bloomberg Clang was not found at:\n"
    "  ${P2996_CLANG}\n"
    "Set CLANG_P2996_ROOT to its installation prefix."
  )
endif()

if(NOT EXISTS "${P2996_CLANGXX}")
  message(FATAL_ERROR
    "Bloomberg Clang++ was not found at:\n"
    "  ${P2996_CLANGXX}"
  )
endif()

if(DEFINED ENV{NYX_LIBCXX_ROOT} AND NOT "$ENV{NYX_LIBCXX_ROOT}" STREQUAL "")
  set(NYX_LIBCXX_ROOT "$ENV{NYX_LIBCXX_ROOT}")
else()
  set(NYX_LIBCXX_ROOT "$ENV{HOME}/.local/opt/clang-p2996")
endif()

set(NYX_LIBCXX_TARGET_INCLUDE
  "${NYX_LIBCXX_ROOT}/include/x86_64-unknown-linux-gnu/c++/v1"
)
set(NYX_LIBCXX_INCLUDE "${NYX_LIBCXX_ROOT}/include/c++/v1")
set(NYX_LIBCXX_LIBRARY
  "${NYX_LIBCXX_ROOT}/lib/x86_64-unknown-linux-gnu"
)

string(APPEND CMAKE_CXX_FLAGS_INIT
  " -nostdinc++"
  " -isystem${NYX_LIBCXX_TARGET_INCLUDE}"
  " -isystem${NYX_LIBCXX_INCLUDE}"
)
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT
  " -stdlib=libc++ -L${NYX_LIBCXX_LIBRARY}"
)
string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT
  " -stdlib=libc++ -L${NYX_LIBCXX_LIBRARY}"
)
string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT
  " -stdlib=libc++ -L${NYX_LIBCXX_LIBRARY}"
)

set(CMAKE_C_COMPILER
  "${P2996_CLANG}"
  CACHE FILEPATH
  "Bloomberg P2996 Clang compiler"
  FORCE
)

set(CMAKE_CXX_COMPILER
  "${P2996_CLANGXX}"
  CACHE FILEPATH
  "Bloomberg P2996 Clang++ compiler"
  FORCE
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_SYSTEM_PROCESSOR)
  set(CMAKE_SYSTEM_PROCESSOR "x86_64" CACHE STRING "Target processor" FORCE)
endif()

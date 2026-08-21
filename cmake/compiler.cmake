add_library(NyxCompilerOptions INTERFACE)
add_library(Nyx::CompilerOptions ALIAS NyxCompilerOptions)

target_compile_features(NyxCompilerOptions INTERFACE cxx_std_26)

set(nyx_compiler_options)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
    message(FATAL_ERROR "Nyx requires GCC 16 or newer when building with GCC.")
  endif(list (APPEND nyx_compiler_options -fmodules -freflection -fexceptions))

  message(STATUS "Nyx C++26 reflection: GCC ${CMAKE_CXX_COMPILER_VERSION}")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  list(APPEND nyx_compiler_options -freflection-latest -fexceptions)

  message(STATUS "Nyx C++26 reflection: Clang ${CMAKE_CXX_COMPILER_VERSION}")
else()
  message(
    WARNING "Nyx has not been validated with "
            "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}. "
            "The selected toolchain must provide the required C++26 reflection "
            "support itself.")
endif()

target_compile_options(NyxCompilerOptions INTERFACE ${nyx_compiler_options})

message(
  STATUS "Nyx exception policy: minimized; compiler exception support retained "
         "for STL contracts")

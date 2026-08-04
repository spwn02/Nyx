add_library(NyxCompilerOptions INTERFACE)
add_library(Nyx::CompilerOptions ALIAS NyxCompilerOptions)

target_compile_features(
  NyxCompilerOptions
  INTERFACE
    cxx_std_26
)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR
    "Nyx requires Clang, but the selected compiler is "
    "${CMAKE_CXX_COMPILER_ID}."
  )
endif()

message(STATUS "Bloomberg P2296 reflection support enabled")

target_compile_options(
  NyxCompilerOptions
  INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-freflection-latest>
)

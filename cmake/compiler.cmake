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

# The experimental std module currently requires exception handling to remain
# enabled for constexpr formatter diagnostics and module-file compatibility.
# Nyx still treats exceptions as an exceptional boundary and audits accidental
# application-level throws separately.
target_compile_options(
  NyxCompilerOptions
  INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-freflection-latest>
    $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
)

message(STATUS "Nyx exception policy: minimized; compiler exception support retained for STL contracts")

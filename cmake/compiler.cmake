add_library(NyxCompilerOptions INTERFACE)
add_library(Nyx::CompilerOptions ALIAS NyxCompilerOptions)

target_compile_features(NyxCompilerOptions INTERFACE cxx_std_26)

message(
  STATUS
    "Nyx C++26 toolchain: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(
  STATUS
    "Nyx compiler, standard-library, reflection, contracts, and module "
    "configuration is owned by the selected external toolchain")

message(
  STATUS "Nyx exception policy: minimized; compiler exception support retained "
         "for STL contracts")

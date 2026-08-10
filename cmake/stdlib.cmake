include_guard(GLOBAL)

if(DEFINED ENV{NYX_LIBCXX_ROOT} AND NOT "$ENV{NYX_LIBCXX_ROOT}" STREQUAL "")
  set(NYX_CLANG_P2996_ROOT_DEFAULT "$ENV{NYX_LIBCXX_ROOT}")
else()
  set(NYX_CLANG_P2996_ROOT_DEFAULT "$ENV{HOME}/.local/opt/clang-p2996")
endif()
set(NYX_CLANG_P2996_ROOT
  "${NYX_CLANG_P2996_ROOT_DEFAULT}"
  CACHE PATH "Experimental libc++ installation used by Nyx"
)
set(NYX_CLANG_P2996_COMPILER_ROOT
  "$ENV{HOME}/.local/opt/clang-p2996"
  CACHE PATH "Bloomberg Clang installation used by Nyx"
)

set(NYX_CLANGXX "${NYX_CLANG_P2996_COMPILER_ROOT}/bin/clang++")
set(NYX_LIBCXX_INCLUDE "${NYX_CLANG_P2996_ROOT}/include/c++/v1")
set(NYX_LIBCXX_TARGET_INCLUDE
  "${NYX_CLANG_P2996_ROOT}/include/${CMAKE_SYSTEM_PROCESSOR}-unknown-linux-gnu/c++/v1"
)
set(NYX_LIBCXX_MODULE_INCLUDE "${NYX_CLANG_P2996_ROOT}/share/libc++/v1")
set(NYX_LIBCXX_SOURCE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../../toolchains/clang-p2996/libcxx"
)
set(NYX_LIBCXX_SOURCE_INCLUDE "${NYX_LIBCXX_SOURCE_DIR}/include")
set(NYX_STDLIB_BINARY_DIR "${CMAKE_BINARY_DIR}/stdlib")
set(NYX_STDLIB_GENERATED_INCLUDE "${NYX_STDLIB_BINARY_DIR}/include")
set(NYX_STDLIB_MODULE "${NYX_STDLIB_BINARY_DIR}/std.pcm")
set(NYX_STDLIB_MODULE_OBJECT "${NYX_STDLIB_BINARY_DIR}/std.o")
set(NYX_LIBCXX_MODULE_SOURCE "${NYX_STDLIB_BINARY_DIR}/std.cppm")
set(NYX_LIBCXX_LIBRARY
  "${NYX_CLANG_P2996_ROOT}/lib/${CMAKE_SYSTEM_PROCESSOR}-unknown-linux-gnu"
)

if(NOT EXISTS "${NYX_CLANGXX}")
  message(FATAL_ERROR "Bloomberg Clang++ not found: ${NYX_CLANGXX}")
endif()

file(GLOB NYX_LIBCXX_MODULE_PARTITIONS
  "${NYX_LIBCXX_SOURCE_DIR}/modules/std/*.inc"
)
set(LIBCXX_MODULE_STD_INCLUDE_SOURCES)
foreach(file IN LISTS NYX_LIBCXX_MODULE_PARTITIONS)
  get_filename_component(name "${file}" NAME)
  string(APPEND LIBCXX_MODULE_STD_INCLUDE_SOURCES
    "#include \"std/${name}\"\n"
  )
endforeach()
file(MAKE_DIRECTORY "${NYX_STDLIB_BINARY_DIR}")
file(MAKE_DIRECTORY "${NYX_STDLIB_GENERATED_INCLUDE}")
configure_file(
  "${NYX_LIBCXX_INCLUDE}/__assertion_handler"
  "${NYX_STDLIB_GENERATED_INCLUDE}/__assertion_handler"
  COPYONLY
)
configure_file(
  "${NYX_LIBCXX_SOURCE_DIR}/modules/std.cppm.in"
  "${NYX_LIBCXX_MODULE_SOURCE}"
  @ONLY
)

add_custom_command(
  OUTPUT "${NYX_STDLIB_MODULE}" "${NYX_STDLIB_MODULE_OBJECT}"
  COMMAND "${NYX_CLANGXX}"
    -std=c++26 -freflection-latest -nostdinc++
    -isystem "${NYX_LIBCXX_TARGET_INCLUDE}"
    -isystem "${NYX_STDLIB_GENERATED_INCLUDE}"
    -isystem "${NYX_LIBCXX_SOURCE_INCLUDE}"
    -isystem "${NYX_LIBCXX_SOURCE_DIR}/modules"
    -isystem "${NYX_LIBCXX_MODULE_INCLUDE}"
    -Wno-reserved-module-identifier
    -Wno-reserved-user-defined-literal
    --precompile -c "${NYX_LIBCXX_MODULE_SOURCE}"
    -o "${NYX_STDLIB_MODULE}"
  COMMAND "${NYX_CLANGXX}"
    -std=c++26 -freflection-latest -nostdinc++
    -isystem "${NYX_LIBCXX_TARGET_INCLUDE}"
    -isystem "${NYX_STDLIB_GENERATED_INCLUDE}"
    -isystem "${NYX_LIBCXX_SOURCE_INCLUDE}"
    -isystem "${NYX_LIBCXX_SOURCE_DIR}/modules"
    -fmodule-file=std=${NYX_STDLIB_MODULE}
    -c "${NYX_LIBCXX_MODULE_SOURCE}"
    -o "${NYX_STDLIB_MODULE_OBJECT}"
  DEPENDS "${NYX_LIBCXX_MODULE_SOURCE}" ${NYX_LIBCXX_MODULE_PARTITIONS}
  VERBATIM
)

add_custom_target(nyx_std_module
  DEPENDS "${NYX_STDLIB_MODULE}" "${NYX_STDLIB_MODULE_OBJECT}"
)
add_library(NyxExperimentalStd INTERFACE)
add_library(Nyx::ExperimentalStd ALIAS NyxExperimentalStd)
add_dependencies(NyxExperimentalStd nyx_std_module)
target_link_libraries(NyxExperimentalStd INTERFACE "${NYX_STDLIB_MODULE_OBJECT}")
target_compile_options(NyxExperimentalStd INTERFACE
  -freflection-latest -nostdinc++
  "-isystem${NYX_LIBCXX_TARGET_INCLUDE}"
  "-isystem${NYX_STDLIB_GENERATED_INCLUDE}"
  "-fmodule-file=std=${NYX_STDLIB_MODULE}"
)
target_link_options(NyxExperimentalStd INTERFACE
  -stdlib=libc++
  "-L${NYX_LIBCXX_LIBRARY}"
  "-Wl,-rpath,${NYX_LIBCXX_LIBRARY}"
)

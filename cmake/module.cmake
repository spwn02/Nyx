include_guard(GLOBAL)

if(CMAKE_VERSION VERSION_LESS 3.28)
  message(FATAL_ERROR
    "module.cmake requires CMake 3.28 or newer for CXX_MODULES file sets")
endif()

# LINK_ONLY must suppress compile-time usage requirements, otherwise an
# internal/private dependency can accidentally expose its module provider.
if(POLICY CMP0131)
  cmake_policy(SET CMP0131 NEW)
endif()

# Find a package and verify that it provides the imported targets Nyx expects.
#
# Example:
#   nyx_require_package(SDL3
#     TARGETS SDL3::SDL3
#     CONFIG
#   )
#
# REQUIRED is implicit. If every requested target already exists and no version
# or components are requested, find_package() is skipped. This makes the helper
# compatible with dependencies supplied by add_subdirectory(), FetchContent, or
# a package manager before this function is called.
function(nyx_require_package package)
  set(options CONFIG MODULE EXACT QUIET GLOBAL ALWAYS_FIND)
  set(one_value_args VERSION)
  set(multi_value_args TARGETS COMPONENTS OPTIONAL_COMPONENTS)

  cmake_parse_arguments(PARSE_ARGV 1 arg
    "${options}" "${one_value_args}" "${multi_value_args}"
  )

  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "nyx_require_package(${package}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(arg_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR
      "nyx_require_package(${package}): missing values for: ${arg_KEYWORDS_MISSING_VALUES}")
  endif()
  if(NOT arg_TARGETS)
    message(FATAL_ERROR
      "nyx_require_package(${package}): TARGETS must list at least one imported target")
  endif()
  if(arg_CONFIG AND arg_MODULE)
    message(FATAL_ERROR
      "nyx_require_package(${package}): CONFIG and MODULE are mutually exclusive")
  endif()
  if(arg_EXACT AND NOT arg_VERSION)
    message(FATAL_ERROR
      "nyx_require_package(${package}): EXACT requires VERSION")
  endif()

  set(all_targets_exist TRUE)
  foreach(required_target IN LISTS arg_TARGETS)
    if(NOT TARGET "${required_target}")
      set(all_targets_exist FALSE)
      break()
    endif()
  endforeach()

  if(all_targets_exist
      AND NOT arg_ALWAYS_FIND
      AND NOT arg_VERSION
      AND NOT arg_COMPONENTS
      AND NOT arg_OPTIONAL_COMPONENTS)
    return()
  endif()

  set(find_arguments)
  if(arg_VERSION)
    list(APPEND find_arguments "${arg_VERSION}")
  endif()
  if(arg_EXACT)
    list(APPEND find_arguments EXACT)
  endif()
  if(arg_QUIET)
    list(APPEND find_arguments QUIET)
  endif()
  if(arg_MODULE)
    list(APPEND find_arguments MODULE)
  elseif(arg_CONFIG)
    list(APPEND find_arguments CONFIG)
  endif()

  # The helper is named "require", so REQUIRED is deliberately implicit.
  list(APPEND find_arguments REQUIRED)

  if(arg_COMPONENTS)
    list(APPEND find_arguments COMPONENTS ${arg_COMPONENTS})
  endif()
  if(arg_OPTIONAL_COMPONENTS)
    list(APPEND find_arguments OPTIONAL_COMPONENTS ${arg_OPTIONAL_COMPONENTS})
  endif()
  if(arg_GLOBAL)
    list(APPEND find_arguments GLOBAL)
  endif()

  find_package(${package} ${find_arguments})

  set(missing_targets)
  foreach(required_target IN LISTS arg_TARGETS)
    if(NOT TARGET "${required_target}")
      list(APPEND missing_targets "${required_target}")
    endif()
  endforeach()

  if(missing_targets)
    list(JOIN missing_targets ", " missing_targets_text)
    message(FATAL_ERROR
      "nyx_require_package(${package}): the package was found, but it did not "
      "provide the required target(s): ${missing_targets_text}")
  endif()
endfunction()

# Create the two project-wide aggregation targets:
#
#   Nyx::Nyx     - public API of every registered module
#   Nyx::Engine  - engine API umbrella; currently the same public API
function(nyx_initialize_module_system)
  if(NOT TARGET Nyx)
    add_library(Nyx INTERFACE)
    add_library(Nyx::Nyx ALIAS Nyx)
  endif()

  if(NOT TARGET NyxEngineAPI)
    add_library(NyxEngineAPI INTERFACE)
    add_library(Nyx::Engine ALIAS NyxEngineAPI)
  endif()
endfunction()

function(_nyx_apply_module_defaults target)
  target_compile_features(${target} PUBLIC cxx_std_26)

  target_compile_options(${target} PRIVATE
    -Wall
    -Wextra
    -pedantic
    -Werror
    -Wconversion-null
    -Wmissing-declarations
    -Woverlength-strings
    -Wpointer-arith
    -Wunused-local-typedefs
    -Wunused-result
    -Wvarargs
    -Wvla
    -Wwrite-strings
    -Wno-missing-declarations
    -Wno-missing-designated-field-initializers
    -Wmodule-file-config-mismatch
  )

  set_target_properties(${target} PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD_REQUIRED ON
    CXX_SCAN_FOR_MODULES ON
  )

  target_link_libraries(${target} PUBLIC Nyx::CompilerOptions)
  if(TARGET Nyx::ExperimentalStd)
    target_link_libraries(${target} PUBLIC Nyx::ExperimentalStd)
    if(TARGET nyx_std_module)
      add_dependencies(${target} nyx_std_module)
    endif()
  endif()
endfunction()

function(_nyx_require_targets owner scope)
  foreach(dependency IN LISTS ARGN)
    if(dependency MATCHES "::" AND NOT TARGET "${dependency}")
      message(FATAL_ERROR
        "${owner}: ${scope} dependency '${dependency}' is not a target. "
        "Call nyx_require_package() or otherwise create the target before "
        "nyx_add_module().")
    endif()
  endforeach()
endfunction()

function(_nyx_collect_sources output owner root dir extension)
  set(result)

  if(ARGN)
    foreach(source IN LISTS ARGN)
      string(REPLACE "\\" "/" relative "${source}")
      if(IS_ABSOLUTE "${relative}" OR relative MATCHES "(^|/)\.\.(/|$)")
        message(FATAL_ERROR
          "${owner}: source '${source}' must stay relative to ${dir}/")
      endif()
      if(relative MATCHES "^(Public|Internal|Private|Shared)/")
        message(FATAL_ERROR
          "${owner}: source '${source}' already contains a visibility root; "
          "pass a path relative to ${dir}/")
      endif()

      set(relative_path "${relative}")
      cmake_path(NORMAL_PATH relative_path)
      set(source_file "${root}/${dir}/${relative_path}")
      if(NOT EXISTS "${source_file}")
        message(FATAL_ERROR "${owner}: source does not exist: ${source_file}")
      endif()

      if(extension)
        cmake_path(GET relative_path EXTENSION LAST_ONLY source_extension)
        if(NOT source_extension STREQUAL extension)
          message(FATAL_ERROR
            "${owner}: ${dir} source '${source}' must use '${extension}'")
        endif()
      endif()

      list(APPEND result "${source_file}")
    endforeach()
  elseif(IS_DIRECTORY "${root}/${dir}")
    if(extension)
      file(GLOB_RECURSE result CONFIGURE_DEPENDS
        "${root}/${dir}/*${extension}")
    else()
      file(GLOB_RECURSE result CONFIGURE_DEPENDS
        "${root}/${dir}/*.c"
        "${root}/${dir}/*.cc"
        "${root}/${dir}/*.cpp"
        "${root}/${dir}/*.cxx"
        "${root}/${dir}/*.m"
        "${root}/${dir}/*.mm")
    endif()
  endif()

  if(result)
    list(REMOVE_DUPLICATES result)
    list(SORT result)
  endif()

  set(${output} ${result} PARENT_SCOPE)
endfunction()

function(nyx_add_module name)
  set(options NO_UMBRELLA)
  set(one_value_args ROOT TYPE MODULE_EXTENSION)
  set(multi_value_args
    PUBLIC INTERNAL PRIVATE
    PUBLIC_DEPS INTERNAL_DEPS PRIVATE_DEPS
  )

  cmake_parse_arguments(PARSE_ARGV 1 arg
    "${options}" "${one_value_args}" "${multi_value_args}"
  )

  set(owner "nyx_add_module(${name})")

  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${owner}: unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(arg_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR "${owner}: missing values for: ${arg_KEYWORDS_MISSING_VALUES}")
  endif()
  if(NOT arg_ROOT)
    set(arg_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  cmake_path(ABSOLUTE_PATH arg_ROOT NORMALIZE OUTPUT_VARIABLE root)

  if(NOT arg_MODULE_EXTENSION)
    set(arg_MODULE_EXTENSION ".ixx")
  endif()
  if(NOT arg_MODULE_EXTENSION MATCHES "^\\.")
    string(PREPEND arg_MODULE_EXTENSION ".")
  endif()

  set(target "Nyx${name}")
  set(engine_facade "${target}__engine")

  foreach(candidate IN ITEMS "${target}" "${engine_facade}")
    if(TARGET "${candidate}")
      message(FATAL_ERROR "${owner}: target '${candidate}' already exists")
    endif()
  endforeach()

  _nyx_collect_sources(_nyx_public_interfaces "${owner}" "${root}" Public
    "${arg_MODULE_EXTENSION}" ${arg_PUBLIC})
  _nyx_collect_sources(_nyx_internal_interfaces "${owner}" "${root}" Internal
    "${arg_MODULE_EXTENSION}" ${arg_INTERNAL})
  _nyx_collect_sources(_nyx_private_sources "${owner}" "${root}" Private "" ${arg_PRIVATE})

  set(_nyx_all_sources
    ${_nyx_public_interfaces}
    ${_nyx_internal_interfaces}
    ${_nyx_private_sources}
  )
  if(NOT _nyx_all_sources)
    message(FATAL_ERROR
      "${owner}: no sources found under ${root}/Public, ${root}/Internal, or ${root}/Private")
  endif()

  if(arg_TYPE)
    if(NOT arg_TYPE MATCHES "^(STATIC|SHARED)$")
      message(FATAL_ERROR "${owner}: TYPE must be STATIC or SHARED")
    endif()
    add_library(${target} ${arg_TYPE})
  else()
    add_library(${target})
  endif()

  add_library(Nyx::${name} ALIAS ${target})
  set_property(TARGET ${target} PROPERTY NYX_BINARY_TARGET ${target})
  _nyx_apply_module_defaults(${target})

  if(_nyx_public_interfaces)
    target_sources(${target}
      PUBLIC FILE_SET public_modules TYPE CXX_MODULES
        BASE_DIRS "${root}/Public"
        FILES ${_nyx_public_interfaces}
    )
  endif()

  if(_nyx_internal_interfaces)
    target_sources(${target}
      PRIVATE FILE_SET internal_modules TYPE CXX_MODULES
        BASE_DIRS "${root}/Internal"
        FILES ${_nyx_internal_interfaces}
    )
  endif()

  if(_nyx_private_sources)
    target_sources(${target} PRIVATE ${_nyx_private_sources})
  endif()

  _nyx_require_targets("${owner}" PUBLIC_DEPS ${arg_PUBLIC_DEPS})
  _nyx_require_targets("${owner}" INTERNAL_DEPS ${arg_INTERNAL_DEPS})
  _nyx_require_targets("${owner}" PRIVATE_DEPS ${arg_PRIVATE_DEPS})

  if(arg_PUBLIC_DEPS)
    target_link_libraries(${target} PUBLIC ${arg_PUBLIC_DEPS})
  endif()
  if(arg_INTERNAL_DEPS OR arg_PRIVATE_DEPS)
    target_link_libraries(${target} PRIVATE ${arg_INTERNAL_DEPS} ${arg_PRIVATE_DEPS})
  endif()

  add_library(${engine_facade} INTERFACE)
  add_library(Nyx::Engine::${name} ALIAS ${engine_facade})
  set_property(TARGET ${engine_facade} PROPERTY NYX_BINARY_TARGET ${target})
  target_link_libraries(${engine_facade} INTERFACE ${target})

  if(NOT arg_NO_UMBRELLA)
    nyx_initialize_module_system()
    target_link_libraries(Nyx INTERFACE ${target})
    target_link_libraries(NyxEngineAPI INTERFACE ${engine_facade})
  endif()

  source_group(TREE "${root}" FILES ${_nyx_all_sources})
endfunction()

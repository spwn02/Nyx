include_guard(GLOBAL)

function(_nyx_set_build_value name value)
  set(${name} "${value}" CACHE INTERNAL "Generated Nyx build configuration" FORCE)
endfunction()

function(nyx_initialize_build_configuration)
  set(NYX_BUILD_PROFILE "Debug" CACHE STRING
    "Nyx build profile: Debug, Development, DevelopmentTests, or Release")

  set_property(CACHE NYX_BUILD_PROFILE PROPERTY STRINGS
    Debug Development DevelopmentTests Release)

  if(NOT NYX_BUILD_PROFILE MATCHES "^(Debug|Development|DevelopmentTests|Release)$")
    message(FATAL_ERROR
      "NYX_BUILD_PROFILE must be Debug, Development, DevelopmentTests, or Release; "
      "received '${NYX_BUILD_PROFILE}'")
  endif()

  option(NYX_ALLOW_UNSUPPORTED_PLATFORM
    "Allow a build target whose operating system does not have a Nyx platform mapping" OFF)

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(platform Linux)
    set(platform_key LINUX)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(platform Windows)
    set(platform_key WINDOWS)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(platform MacOS)
    set(platform_key MACOS)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(platform IOS)
    set(platform_key IOS)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(platform Android)
    set(platform_key ANDROID)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(platform FreeBSD)
    set(platform_key FREEBSD)
  elseif(NYX_ALLOW_UNSUPPORTED_PLATFORM)
    set(platform Unsupported)
    set(platform_key UNSUPPORTED)
  else()
    message(FATAL_ERROR
      "Nyx does not support the CMake target system '${CMAKE_SYSTEM_NAME}'. "
      "Set NYX_ALLOW_UNSUPPORTED_PLATFORM=ON only while bringing up a new platform.")
  endif()

  if(NYX_BUILD_PROFILE STREQUAL "Debug")
    set(optimized false)
    set(development true)
    set(assertions true)
    set(expensive_checks true)
    set(gpu_validation true)
    set(profiling true)
    set(tests false)
    set(exceptions false)
  elseif(NYX_BUILD_PROFILE STREQUAL "Development")
    set(optimized true)
    set(development true)
    set(assertions true)
    set(expensive_checks true)
    set(gpu_validation true)
    set(profiling true)
    set(tests false)
    set(exceptions false)
  elseif(NYX_BUILD_PROFILE STREQUAL "DevelopmentTests")
    set(optimized true)
    set(development true)
    set(assertions true)
    set(expensive_checks true)
    set(gpu_validation true)
    set(profiling true)
    set(tests true)
    set(exceptions true)
  else()
    set(optimized true)
    set(development false)
    set(assertions false)
    set(expensive_checks false)
    set(gpu_validation false)
    set(profiling false)
    set(tests false)
    set(exceptions false)
  endif()

  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" processor)
  if(processor MATCHES "^(x86_64|amd64)$")
    set(architecture X86_64)
  elseif(processor MATCHES "^(aarch64|arm64)$")
    set(architecture AArch64)
  else()
    set(architecture Unknown)
  endif()

  _nyx_set_build_value(NYX_BUILD_PLATFORM "${platform}")
  _nyx_set_build_value(NYX_BUILD_PLATFORM_KEY "${platform_key}")
  _nyx_set_build_value(NYX_BUILD_ARCHITECTURE "${architecture}")
  _nyx_set_build_value(NYX_BUILD_PROFILE_NAME "${NYX_BUILD_PROFILE}")
  _nyx_set_build_value(NYX_BUILD_OPTIMIZED "${optimized}")
  _nyx_set_build_value(NYX_BUILD_DEVELOPMENT "${development}")
  _nyx_set_build_value(NYX_BUILD_ASSERTIONS "${assertions}")
  _nyx_set_build_value(NYX_BUILD_EXPENSIVE_CHECKS "${expensive_checks}")
  _nyx_set_build_value(NYX_BUILD_GPU_VALIDATION "${gpu_validation}")
  _nyx_set_build_value(NYX_BUILD_PROFILING "${profiling}")
  _nyx_set_build_value(NYX_BUILD_TESTS "${tests}")
  _nyx_set_build_value(NYX_BUILD_EXCEPTIONS "${exceptions}")

  message(STATUS
    "Nyx build profile: ${NYX_BUILD_PROFILE}; target platform: ${NYX_BUILD_PLATFORM}")
endfunction()

function(nyx_verify_exception_boundary)
  set(options)
  set(one_value_args ROOT)
  set(multi_value_args)

  cmake_parse_arguments(PARSE_ARGV 0 arg
    "${options}" "${one_value_args}" "${multi_value_args}")

  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "nyx_verify_exception_boundary(): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()

  if(arg_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR
      "nyx_verify_exception_boundary(): missing values for: ${arg_KEYWORDS_MISSING_VALUES}")
  endif()

  if(NOT arg_ROOT)
    set(arg_ROOT "${CMAKE_SOURCE_DIR}")
  endif()

  cmake_path(ABSOLUTE_PATH arg_ROOT NORMALIZE OUTPUT_VARIABLE root)

  file(GLOB_RECURSE sources CONFIGURE_DEPENDS
    "${root}/Engine/Source/*.ixx"
    "${root}/Engine/Source/*.cxx"
    "${root}/Engine/Source/*.cpp"
    "${root}/Engine/Source/*.cc"
  )

  set(violations)

  foreach(source IN LISTS sources)
    string(REPLACE "\\" "/" normalized_source "${source}")

    # Nyx.Test is an API-only dependency in production profiles. Its test
    # exception island is linked only by the DevelopmentTests executable.
    if(normalized_source MATCHES "/Runtime/Test/")
      continue()
    endif()

    # std::formatter::parse is an intentional exception boundary: the standard
    # format checker uses format_error during constant evaluation. This is a
    # toolchain contract, not application-level exception-based control flow.
    if(normalized_source MATCHES "/Runtime/Core/Public/Meta/(Bitflags|Debug)\\.ixx$")
      continue()
    endif()

    # The regular expression intentionally checks statement-leading throws.
    # Catching STL failures remains permitted; accidental application-level
    # throws and explicit exception propagation remain forbidden.
    file(STRINGS "${source}" forbidden
      REGEX "^[ \t]*throw([ \t({;:]|$)|^[^/]*std::(rethrow_exception|make_exception_ptr)")

    if(forbidden)
      list(APPEND violations "${source}")
    endif()
  endforeach()

  if(violations)
    list(JOIN violations "\n  " violations_text)

    message(FATAL_ERROR
      "Unexpected application-level exception syntax in production sources.\n  ${violations_text}")
  endif()
endfunction()

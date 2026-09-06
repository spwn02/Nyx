# Binary release packaging is derived exclusively from the CMake install tree.
# CI orchestrates this configuration but does not hand-build release archives.

if(NOT TARGET NyxEngine)
  message(FATAL_ERROR "Nyx packaging requires the NyxEngine target")
endif()

set(NYX_PACKAGE_VERSION
    "${PROJECT_VERSION}"
    CACHE
      STRING
      "Version string embedded in Nyx binary package names (release tags may override it)"
)

if(NYX_PACKAGE_VERSION STREQUAL "")
  message(FATAL_ERROR "NYX_PACKAGE_VERSION must not be empty")
endif()

string(TOLOWER "${CMAKE_SYSTEM_NAME}" _nyx_package_system)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  set(_nyx_package_arch "x86_64")
else()
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _nyx_package_arch)
endif()

if(_nyx_package_system STREQUAL "" OR _nyx_package_arch STREQUAL "")
  message(
    FATAL_ERROR "Nyx could not determine the target platform for packaging")
endif()

# Keep the executable relocatable with respect to the toolchain runtime bundled
# beside it. System libraries are intentionally not copied into the archive.
set_target_properties(NyxEngine PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")

set(_nyx_toolchain_root "$ENV{CXX26_TOOLCHAIN_ROOT}")
if(NOT _nyx_toolchain_root STREQUAL "")
  cmake_path(NORMAL_PATH _nyx_toolchain_root)

  install(
    TARGETS NyxEngine RUNTIME_DEPENDENCIES POST_INCLUDE_REGEXES
            "^${_nyx_toolchain_root}/" POST_EXCLUDE_REGEXES ".*"
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)
else()
  message(
    WARNING
      "CXX26_TOOLCHAIN_ROOT is not set; the package will not bundle runtime "
      "libraries from the selected C++26 toolchain")
  install(TARGETS NyxEngine RUNTIME DESTINATION bin)
endif()

install(
  FILES "${PROJECT_SOURCE_DIR}/README.md" "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
        "${PROJECT_SOURCE_DIR}/CONTRIBUTING.md"
        "${PROJECT_SOURCE_DIR}/SECURITY.md" DESTINATION "share/doc/NyxEngine")

install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/COPYRIGHT"
              "${PROJECT_SOURCE_DIR}/licenses/GPL-3.0.txt"
        DESTINATION "share/licenses/NyxEngine")

install(FILES "${PROJECT_SOURCE_DIR}/licenses/LLVM-LICENSE.txt"
        DESTINATION "share/licenses/NyxEngine/third-party/clang-cxx26")

# Miracle and Switch are independent products with independent licenses.
foreach(_nyx_product IN ITEMS Miracle Switch)
  set(_nyx_product_root
      "${PROJECT_SOURCE_DIR}/Engine/Source/Runtime/${_nyx_product}")

  if(EXISTS "${_nyx_product_root}/LICENSE")
    install(FILES "${_nyx_product_root}/LICENSE"
            DESTINATION "share/licenses/NyxEngine/third-party/${_nyx_product}")
  endif()

  foreach(_nyx_notice IN ITEMS NOTICE NOTICE.txt)
    if(EXISTS "${_nyx_product_root}/${_nyx_notice}")
      install(
        FILES "${_nyx_product_root}/${_nyx_notice}"
        DESTINATION "share/licenses/NyxEngine/third-party/${_nyx_product}")
    endif()
  endforeach()
endforeach()

# Preserve target-port copyright/license notices for every installed vcpkg
# dependency, including transitive dependencies linked into the executable.
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
  set(_nyx_vcpkg_share "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")

  if(IS_DIRECTORY "${_nyx_vcpkg_share}")
    install(
      DIRECTORY "${_nyx_vcpkg_share}/"
      DESTINATION "share/licenses/NyxEngine/third-party/vcpkg"
      FILES_MATCHING
      PATTERN "copyright")
  endif()
endif()

set(CPACK_PACKAGE_NAME "NyxEngine")
set(CPACK_PACKAGE_VENDOR "Spawn")
set(CPACK_PACKAGE_VERSION "${NYX_PACKAGE_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/spwn02/Nyx")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${_nyx_package_system}-${_nyx_package_arch}"
)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
set(CPACK_GENERATOR "TZST")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")

include(CPack)

unset(_nyx_package_arch)
unset(_nyx_package_system)
unset(_nyx_product)
unset(_nyx_product_root)
unset(_nyx_notice)
unset(_nyx_toolchain_root)
unset(_nyx_vcpkg_share)

# find_package(Catch2 3 REQUIRED)

add_executable(unit_tests EXCLUDE_FROM_ALL)

file(GLOB_RECURSE framework_tests "${CMAKE_CURRENT_SOURCE_DIR}/framework/*.cxx")
file(GLOB_RECURSE core_tests "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cxx")

target_sources(unit_tests
  PRIVATE
    ${framework_tests}
    ${core_tests}
    # bitflags.cpp
    # debug.cpp
    # error.cpp
    # fs.cpp
    # platform_folders.cpp
    # reflection.cpp
    # rhi.cpp
    tests.cpp
)

target_link_libraries(unit_tests
  PRIVATE
    # Catch2::Catch2WithMain
    Nyx
    Nyx::Engine
)

add_dependencies(unit_tests nyx_std_module)

# include(Catch)
# catch_discover_tests(unit_tests)

# find_package(Catch2 3 REQUIRED)

add_executable(unit_tests EXCLUDE_FROM_ALL)

target_sources(unit_tests
  PRIVATE
    bitflags.cpp
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

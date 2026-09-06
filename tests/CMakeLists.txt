add_executable(unit_tests EXCLUDE_FROM_ALL)

file(GLOB_RECURSE rhi_tests "${CMAKE_CURRENT_SOURCE_DIR}/rhi/*.cxx")

target_sources(unit_tests PRIVATE ${rhi_tests} main.cpp)

target_link_libraries(unit_tests Nyx Nyx::Engine Switch::Switch)

add_test(NAME unit_tests COMMAND unit_tests)

# Standalone foundation products are always consumed from the editable checkouts
# embedded in Nyx. Reject a pre-existing package target so a superproject cannot
# silently replace either checkout with an installed copy.
if(TARGET Miracle::Miracle OR Switch::Switch)
  message(
    FATAL_ERROR
      "Nyx requires its in-tree Miracle and Switch checkouts. "
      "Do not predefine Miracle::Miracle or Switch::Switch before adding Nyx.")
endif()

# Their self-tests are separate products and must not be pulled into the Nyx
# integration graph. Nyx owns only its own integration tests here.
set(MIRACLE_BUILD_TESTS OFF)
set(SWITCH_BUILD_TESTS OFF)

add_subdirectory(Miracle)
add_subdirectory(Switch)

foreach(product IN ITEMS Miracle Switch)
  if(NOT TARGET "${product}" OR NOT TARGET "${product}::${product}")
    message(FATAL_ERROR "Nyx failed to create the in-tree ${product} target.")
  endif()

  get_target_property(product_source_dir "${product}" SOURCE_DIR)
  cmake_path(NORMAL_PATH product_source_dir)

  set(expected_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/${product}")
  cmake_path(NORMAL_PATH expected_source_dir)

  if(NOT product_source_dir STREQUAL expected_source_dir)
    message(
      FATAL_ERROR
        "${product} resolved from '${product_source_dir}', but Nyx requires "
        "the editable checkout at '${expected_source_dir}'")
  endif()
endforeach()

# Engine modules
add_subdirectory(Log)
add_subdirectory(Event)
add_subdirectory(Platform)
add_subdirectory(Project)
add_subdirectory(RHI)
add_subdirectory(Renderer)
add_subdirectory(Kernel)

# App
add_subdirectory(Launch)

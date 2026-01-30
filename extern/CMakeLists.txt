add_subdirectory(glm)
add_subdirectory(spdlog)
add_subdirectory(json)

target_compile_definitions(glm PUBLIC GLM_ENABLE_EXPERIMENTAL)

# GLFW builds as a normal CMake project
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)

# ImGui and glad/blake3/xxhash: we create our own targets
add_library(nyx_thirdparty INTERFACE)
add_library(nyx::thirdparty ALIAS nyx_thirdparty)

# ---- glad
add_library(glad STATIC
  ${CMAKE_CURRENT_LIST_DIR}/glad/src/glad.c
)
target_include_directories(glad PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/glad/include
)

# ---- blake3
add_library(blake3 STATIC
  ${CMAKE_CURRENT_LIST_DIR}/blake3/blake3.c
  ${CMAKE_CURRENT_LIST_DIR}/blake3/blake3_dispatch.c
  ${CMAKE_CURRENT_LIST_DIR}/blake3/blake3_portable.c
)
target_include_directories(blake3 PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/blake3
)

# ---- xxhash (header-only)
add_library(xxhash INTERFACE)
target_include_directories(xxhash INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/xxhash
)

# ---- ImGui
add_library(imgui STATIC
  ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui.cpp
  ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_draw.cpp
  ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_tables.cpp
  ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_widgets.cpp
  ${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_demo.cpp
  ${CMAKE_CURRENT_LIST_DIR}/imgui/backends/imgui_impl_glfw.cpp
  ${CMAKE_CURRENT_LIST_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/imgui
  ${CMAKE_CURRENT_LIST_DIR}/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw glad)

# ---- ImGuizmo
add_library(imguizmo STATIC
  ${CMAKE_CURRENT_LIST_DIR}/ImGuizmo/ImGuizmo.cpp
)

target_include_directories(imguizmo PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/ImGuizmo
)

target_link_libraries(imguizmo PUBLIC imgui)

# ---- stb
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE stb)

# Aggregate interface
target_link_libraries(nyx_thirdparty INTERFACE
  glm::glm
  spdlog::spdlog
  glfw
  glad
  blake3
  xxhash
  imgui
  imguizmo
  stb
  nlohmann_json
)

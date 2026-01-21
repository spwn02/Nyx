cmake_minimum_required(VERSION 3.20)
project(Nyx LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# nice defaults
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(extern)

find_package(OpenGL REQUIRED)

file(GLOB_RECURSE NYX_ENGINE_SOURCES CONFIGURE_DEPENDS
  engine/*.cpp
  engine/*.h
)
add_library(nyx_engine STATIC
  ${NYX_ENGINE_SOURCES}
)
target_include_directories(nyx_engine PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/engine
)
target_link_libraries(nyx_engine PUBLIC
  nyx::thirdparty
  OpenGL::GL
)

add_executable(nyx_app
  app/main.cpp
)
target_link_libraries(nyx_app PRIVATE nyx_engine)


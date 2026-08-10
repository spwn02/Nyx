if(NYX_BUILD_PLATFORM STREQUAL "Linux")
  message(STATUS "Nyx launch target: Linux")
else()
  message(FATAL_ERROR "NyxEngine has no launch source for ${NYX_BUILD_PLATFORM}")
endif()

add_executable(NyxEngine "Private/Linux/main.cpp")

target_link_libraries(NyxEngine PRIVATE Nyx::Engine)

install(TARGETS NyxEngine DESTINATION bin)

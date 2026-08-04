if(LINUX)
  message(STATUS "On Linux!")
else()
  message(FATAL_ERROR "Your OS is currently unsupported!")
endif()

add_executable(NyxEngine "Private/Linux/main.cpp")

target_link_libraries(NyxEngine PRIVATE Nyx::Engine)

install(TARGETS NyxEngine DESTINATION bin)

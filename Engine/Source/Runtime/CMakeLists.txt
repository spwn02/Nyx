# Engine
add_subdirectory(Build)
add_subdirectory(Core)
add_subdirectory(Log)
add_subdirectory(Event)
add_subdirectory(Platform)
add_subdirectory(Project)
add_subdirectory(RHI)
add_subdirectory(Renderer)
add_subdirectory(Kernel)
add_subdirectory(Test)

# App
add_subdirectory(Launch)

# Scripts find_package(Python3 REQUIRED COMPONENTS Interpreter)
#
# set(SCRIPTS_FOLDER "${CMAKE_SOURCE_DIR}/scripts") set(ASSETS_FOLDER
# "${CMAKE_CURRENT_SOURCE_DIR}/Assets") set(SHADERS_FOLDER
# "${ASSETS_FOLDER}/Shaders") set(SHADERS_MANIFEST
# "${SHADERS_FOLDER}/manifest.json")
#
# add_custom_command( TARGET NyxEngine PRE_BUILD COMMAND Python3::Interpreter
# "${SCRIPTS_FOLDER}/compile_shaders.py" --output "${SHADERS_MANIFEST}" --dir
# "${SHADERS_FOLDER}" COMMENT "Compiling shaders to SPIR-V..." VERBATIM )

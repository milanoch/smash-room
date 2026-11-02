cmake_minimum_required(VERSION 3.16)
project(SmashLabScripts LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(BIN_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${BIN_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BIN_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BIN_DIRECTORY}")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-DPROFILE)
endif ()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE INCLUDE CONFIGURE_DEPENDS src/*.hpp)

add_library(SmashLabScripts SHARED ${SOURCES} "SMASH LAB.def" ${INCLUDE})

set_target_properties(SmashLabScripts
        PROPERTIES
        OUTPUT_NAME "SMASH LAB"
        ARCHIVE_OUTPUT_DIRECTORY "${BIN_DIRECTORY}"
        LIBRARY_OUTPUT_DIRECTORY "${BIN_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY "${BIN_DIRECTORY}"
)

# Multi-config generators (Visual Studio, Xcode) append /<CONFIG> to the output
# directories unless the per-config properties are set; the engine loads the
# script library from bin/ directly.
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set_target_properties(SmashLabScripts
            PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${BIN_DIRECTORY}"
            LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${BIN_DIRECTORY}"
            RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${BIN_DIRECTORY}"
    )
endforeach()

# add_custom_command(TARGET SmashLabScripts POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:SmashLabScripts> "${BIN_DIRECTORY}"
# )

target_include_directories(SmashLabScripts PUBLIC "${PROJECT_SOURCE_DIR}/src")

find_package(PandaSDK CONFIG REQUIRED)
target_link_libraries(SmashLabScripts PandaSDK::PandaSDK)

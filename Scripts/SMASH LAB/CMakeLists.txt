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
get_filename_component(PANDA_SDK_ROOT "${PandaSDK_DIR}/../../.." ABSOLUTE)
target_include_directories(SmashLabScripts PRIVATE "${PANDA_SDK_ROOT}/modules/Physics/engine/include")

if (APPLE)
    # RgbLampController uses the installed SDK's public native PointLight component.
    # Resolve only these verified editor exports from the host, without linking a
    # second copy of Panda or disabling undefined-symbol checks for the bundle.
    target_link_options(SmashLabScripts PRIVATE
            "LINKER:-U,__ZN5Panda11GameContext15getCurrentWorldEv"
            "LINKER:-U,__ZN5Panda5World7getByIdEN4Rain4UUIDE"
            "LINKER:-U,__ZNK5Panda6Entity16worldGetRegistryEv"
            "LINKER:-U,__ZNK5Panda6Entity7isValidEv"
            "LINKER:-U,__ZN4Rain4UUIDC1Ej"
            # FurnitureDestructionSystem uses the public native physics components
            # to retain the authored collider settings across break/Reset.
            "LINKER:-U,__ZN5Panda6Entity15setWorldChangedEb"
            "LINKER:-U,__ZN5Panda6Entity17componentsChangedEj"
            "LINKER:-U,__ZN5Panda5World28getWorldSpaceTransformMatrixENS_6EntityE"
            "LINKER:-U,__ZN4Rain4UUIDC1Ev"
            "LINKER:-U,__ZN5Panda8AssetRefINS_5AssetEEC1Ev"
            "LINKER:-U,__ZN5Panda8AssetRefINS_5AssetEEC1ERKS2_"
            "LINKER:-U,__ZN5Panda8AssetRefINS_5AssetEED1Ev"
            "LINKER:-U,__ZN5Panda8AssetRefINS_5AssetEEaSERKS2_"
    )
endif ()

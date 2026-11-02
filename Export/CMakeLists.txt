cmake_minimum_required(VERSION 3.20)
project(SmashLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)

# Platform detection
if(APPLE)
    if(IOS)
        set(PLATFORM_IOS TRUE)
    else()
        set(PLATFORM_MACOS TRUE)
    endif()
elseif(ANDROID)
    set(PLATFORM_ANDROID TRUE)
elseif(WIN32)
    set(PLATFORM_WIN32 TRUE)
else()
    set(PLATFORM_LINUX TRUE)
endif()

# Find PandaSDK - provides pre-compiled runtime + scripting API
find_package(PandaSDK CONFIG REQUIRED)

if(NOT EXISTS "${PandaSDK_ENGINE_COMMON_RESOURCES_DIR}")
    message(FATAL_ERROR "PandaSDK common resources not found: ${PandaSDK_ENGINE_COMMON_RESOURCES_DIR}")
endif()

if(NOT EXISTS "${PandaSDK_ENGINE_SHADER_RESOURCES_DIR}")
    message(FATAL_ERROR "PandaSDK shader resources not found: ${PandaSDK_ENGINE_SHADER_RESOURCES_DIR}")
endif()

set(APP_SOURCES
    App/StartApp.cpp
)

get_filename_component(GAME_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(GAME_SCRIPT_DIR "${GAME_PROJECT_ROOT}/Scripts/SMASH LAB/src")
set(PANDA_GAME_RUNTIME_DATA_DIR "${GAME_PROJECT_ROOT}/.Panda/Cooked/Runtime")

# ---- Modules: engine parts are linked statically from sources (context/plugins.md) ----
# Built-in modules of the SDK (<sdk>/modules/*): each folder with module.json enabled in the
# project (modules.txt cooked by Export Game) is added as a static library (panda_package()
# from the SDK), and the generated PandaModules.cpp (pandaRegisterModules) registers them
# before the first world, like pandaLoadScriptBundle does for the scripts.
get_filename_component(PANDA_SDK_ROOT "${PandaSDK_DIR}/../../.." ABSOLUTE)
set(PANDA_MODULE_SEARCH_DIRS "${PANDA_SDK_ROOT}/modules")
set(PANDA_GAME_MODULES_FILE "${PANDA_GAME_RUNTIME_DATA_DIR}/modules.txt")
set(PANDA_GAME_MODULES "")
set(PANDA_GAME_MODULES_ALL FALSE)
set(PANDA_UNLISTED_MODULE_RESOURCE_FOLDERS "") # Resources/<folder> of the modules the game does not list
if(EXISTS "${PANDA_GAME_MODULES_FILE}")
    file(STRINGS "${PANDA_GAME_MODULES_FILE}" PANDA_GAME_MODULES)
else()
    message(WARNING "Cooked module list not found (${PANDA_GAME_MODULES_FILE}); every module found is linked. Run Export Game in PandaEditor.")
    set(PANDA_GAME_MODULES_ALL TRUE)
endif()
foreach(_PANDA_MODULE_SEARCH_DIR ${PANDA_MODULE_SEARCH_DIRS})
    file(GLOB _PANDA_MODULE_MANIFESTS "${_PANDA_MODULE_SEARCH_DIR}/*/module.json")
    foreach(_PANDA_MODULE_MANIFEST ${_PANDA_MODULE_MANIFESTS})
        get_filename_component(_PANDA_MODULE_DIR "${_PANDA_MODULE_MANIFEST}" DIRECTORY)
        if(NOT EXISTS "${_PANDA_MODULE_DIR}/CMakeLists.txt")
            continue()
        endif()
        file(READ "${_PANDA_MODULE_MANIFEST}" _PANDA_MODULE_JSON)
        string(JSON _PANDA_MODULE_ID GET "${_PANDA_MODULE_JSON}" id)
        string(JSON _PANDA_MODULE_PART_COUNT ERROR_VARIABLE _PANDA_MODULE_JSON_ERROR LENGTH "${_PANDA_MODULE_JSON}" parts)
        set(_PANDA_MODULE_HAS_ENGINE FALSE)
        if(NOT _PANDA_MODULE_JSON_ERROR AND _PANDA_MODULE_PART_COUNT GREATER 0)
            math(EXPR _PANDA_MODULE_LAST "${_PANDA_MODULE_PART_COUNT} - 1")
            foreach(_PANDA_MODULE_INDEX RANGE ${_PANDA_MODULE_LAST})
                string(JSON _PANDA_MODULE_PART GET "${_PANDA_MODULE_JSON}" parts ${_PANDA_MODULE_INDEX})
                if(_PANDA_MODULE_PART STREQUAL "engine")
                    set(_PANDA_MODULE_HAS_ENGINE TRUE)
                endif()
            endforeach()
        endif()
        if(NOT _PANDA_MODULE_HAS_ENGINE)
            continue()
        endif()
        if(NOT PANDA_GAME_MODULES_ALL AND NOT "${_PANDA_MODULE_ID}" IN_LIST PANDA_GAME_MODULES)
            # The SDK resources carry the folders of every module; the ones of a module the game
            # does not use are removed from the bundle after the copy (below).
            file(GLOB _PANDA_MODULE_RESOURCE_FOLDERS LIST_DIRECTORIES true "${_PANDA_MODULE_DIR}/engine/resources/*")
            foreach(_PANDA_MODULE_RESOURCE_FOLDER ${_PANDA_MODULE_RESOURCE_FOLDERS})
                if(IS_DIRECTORY "${_PANDA_MODULE_RESOURCE_FOLDER}")
                    get_filename_component(_PANDA_MODULE_RESOURCE_FOLDER_NAME "${_PANDA_MODULE_RESOURCE_FOLDER}" NAME)
                    list(APPEND PANDA_UNLISTED_MODULE_RESOURCE_FOLDERS "${_PANDA_MODULE_RESOURCE_FOLDER_NAME}")
                endif()
            endforeach()
            continue()
        endif()
        string(MAKE_C_IDENTIFIER "${_PANDA_MODULE_ID}" _PANDA_MODULE_C_ID)
        add_subdirectory("${_PANDA_MODULE_DIR}" "modules/${_PANDA_MODULE_C_ID}")
    endforeach()
endforeach()
get_property(PANDA_STATIC_MODULE_TARGETS GLOBAL PROPERTY PANDA_STATIC_MODULE_TARGETS)
get_property(PANDA_STATIC_MODULE_ENTRIES GLOBAL PROPERTY PANDA_STATIC_MODULE_ENTRIES)
get_property(PANDA_STATIC_MODULE_IDS GLOBAL PROPERTY PANDA_STATIC_MODULE_IDS)
get_property(PANDA_STATIC_MODULE_VERSIONS GLOBAL PROPERTY PANDA_STATIC_MODULE_VERSIONS)
set(PANDA_MODULES_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/PandaModules.cpp")
set(_PANDA_MODULES_DECLARATIONS "")
set(_PANDA_MODULES_CALLS "")
list(LENGTH PANDA_STATIC_MODULE_ENTRIES _PANDA_MODULE_COUNT)
if(_PANDA_MODULE_COUNT GREATER 0)
    math(EXPR _PANDA_MODULE_LAST "${_PANDA_MODULE_COUNT} - 1")
    foreach(_PANDA_MODULE_INDEX RANGE ${_PANDA_MODULE_LAST})
        list(GET PANDA_STATIC_MODULE_ENTRIES ${_PANDA_MODULE_INDEX} _PANDA_MODULE_ENTRY)
        list(GET PANDA_STATIC_MODULE_IDS ${_PANDA_MODULE_INDEX} _PANDA_MODULE_ID)
        list(GET PANDA_STATIC_MODULE_VERSIONS ${_PANDA_MODULE_INDEX} _PANDA_MODULE_VERSION)
        string(APPEND _PANDA_MODULES_DECLARATIONS "void ${_PANDA_MODULE_ENTRY}(Panda::Extensions &);\n")
        string(APPEND _PANDA_MODULES_CALLS
            "    loader.loadStatic(\"${_PANDA_MODULE_ID}\", \"${_PANDA_MODULE_VERSION}\", ${_PANDA_MODULE_ENTRY});\n")
    endforeach()
endif()
file(WRITE "${PANDA_MODULES_SOURCE}"
    "// Generated by Export/CMakeLists.txt: modules linked statically into the game.\n"
    "#include <Panda/Extensions/Extensions.hpp>\n"
    "#include <Panda/GameLogic/GameContext.hpp>\n"
    "#include <Panda/Packages/PackageLoader.hpp>\n\n"
    "${_PANDA_MODULES_DECLARATIONS}\n"
    "extern \"C\" void pandaRegisterModules() {\n"
    "    Panda::PackageLoader &loader = Panda::GameContext::getPackageLoader();\n"
    "    (void)loader;\n"
    "${_PANDA_MODULES_CALLS}"
    "}\n"
)
list(APPEND APP_SOURCES "${PANDA_MODULES_SOURCE}")

if(EXISTS "${GAME_SCRIPT_DIR}")
    file(GLOB_RECURSE GAME_SCRIPT_SOURCES CONFIGURE_DEPENDS
        "${GAME_SCRIPT_DIR}/*.cpp"
        "${GAME_SCRIPT_DIR}/*.cc"
        "${GAME_SCRIPT_DIR}/*.cxx"
        "${GAME_SCRIPT_DIR}/*.mm"
    )
    file(GLOB_RECURSE GAME_SCRIPT_HEADERS CONFIGURE_DEPENDS
        "${GAME_SCRIPT_DIR}/*.hpp"
        "${GAME_SCRIPT_DIR}/*.h"
    )
    list(APPEND APP_SOURCES ${GAME_SCRIPT_SOURCES} ${GAME_SCRIPT_HEADERS})
else()
    message(STATUS "No game script source directory found: ${GAME_SCRIPT_DIR}")
endif()

if(PLATFORM_MACOS OR PLATFORM_LINUX OR PLATFORM_WIN32)
    list(APPEND APP_SOURCES EntryPoint/Desktop/main.cpp)
elseif(PLATFORM_IOS)
    list(APPEND APP_SOURCES EntryPoint/IOS/main.mm)
endif()

# ---- App executable ----
# Platform entry point and app startup are generated in this project.
# Engine/runtime implementation is pre-compiled in PandaSDK.
if(PLATFORM_MACOS)
    set(APPLE_RESOURCES native/OSX/AssetsOSX.xcassets)
    add_executable(SmashLab MACOSX_BUNDLE ${APP_SOURCES} ${APPLE_RESOURCES})
    set_target_properties(SmashLab PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/native/OSX/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.company.SMASH LAB"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        RESOURCE "${APPLE_RESOURCES}"
    )
    target_compile_definitions(SmashLab PUBLIC GL_SILENCE_DEPRECATION)
elseif(PLATFORM_IOS)
    set(APPLE_RESOURCES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/native/IOS/LaunchScreen.storyboard")
        list(APPEND APPLE_RESOURCES native/IOS/LaunchScreen.storyboard)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/native/IOS/Assets.xcassets")
        list(APPEND APPLE_RESOURCES native/IOS/Assets.xcassets)
    endif()
    add_executable(SmashLab MACOSX_BUNDLE ${APP_SOURCES} ${APPLE_RESOURCES})
    set_target_properties(SmashLab PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/native/IOS/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.company.SMASH LAB"
        RESOURCE "${APPLE_RESOURCES}"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
        BUILD_RPATH "@executable_path"
    )
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/native/IOS/Assets.xcassets")
        set_target_properties(SmashLab PROPERTIES XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
    endif()
    target_compile_definitions(SmashLab PUBLIC GL_SILENCE_DEPRECATION GLES_SILENCE_DEPRECATION)
elseif(PLATFORM_LINUX)
    add_executable(SmashLab ${APP_SOURCES})
elseif(PLATFORM_WIN32)
    # The WINDOWS subsystem (no console), but the entry point is the ordinary main()
    # from EntryPoint/Desktop/main.cpp, not WinMain.
    add_executable(SmashLab WIN32 ${APP_SOURCES})
    target_link_options(SmashLab PRIVATE "/ENTRY:mainCRTStartup")
elseif(PLATFORM_ANDROID)
    add_library(SmashLab SHARED ${APP_SOURCES})
endif()

# Keep the game name independent of the internal CMake target.
set_target_properties(SmashLab PROPERTIES OUTPUT_NAME "SMASH LAB")

# Link against pre-compiled runtime. Entry point remains in generated app sources.
# Module libraries go first: their engine references resolve against the runtime libraries.
target_link_libraries(SmashLab ${PANDA_STATIC_MODULE_TARGETS} PandaSDK::PandaRuntime)
if(EXISTS "${GAME_SCRIPT_DIR}")
    target_include_directories(SmashLab PRIVATE "${GAME_SCRIPT_DIR}")
endif()

if(NOT EXISTS "${PANDA_GAME_RUNTIME_DATA_DIR}/project.bin")
    message(WARNING "Cooked runtime data not found. Run Export Game in PandaEditor before packaging: ${PANDA_GAME_RUNTIME_DATA_DIR}")
endif()
set(PANDA_CHECK_RUNTIME_DATA_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/CheckRuntimeData.cmake")
file(WRITE "${PANDA_CHECK_RUNTIME_DATA_SCRIPT}"
    "if(NOT EXISTS \"${PANDA_GAME_RUNTIME_DATA_DIR}/project.bin\")\n"
    "    message(FATAL_ERROR \"Cooked runtime data not found. Run Export Game in PandaEditor before packaging: ${PANDA_GAME_RUNTIME_DATA_DIR}\")\n"
    "endif()\n"
)
add_custom_target(PandaCheckRuntimeData
    COMMAND ${CMAKE_COMMAND} -P "${PANDA_CHECK_RUNTIME_DATA_SCRIPT}"
    COMMENT "Checking cooked runtime data"
)
add_dependencies(SmashLab PandaCheckRuntimeData)

# Copy engine resources and cooked game runtime data into fixed runtime locations
if(PLATFORM_MACOS)
    set(APP_RESOURCES_OUTPUT_DIR "$<TARGET_FILE_DIR:SmashLab>/../Resources")
elseif(PLATFORM_IOS)
    set(APP_RESOURCES_OUTPUT_DIR "$<TARGET_FILE_DIR:SmashLab>")
elseif(PLATFORM_LINUX OR PLATFORM_WIN32)
    set(APP_RESOURCES_OUTPUT_DIR "$<TARGET_FILE_DIR:SmashLab>")
endif()

if(PLATFORM_MACOS OR PLATFORM_IOS OR PLATFORM_LINUX OR PLATFORM_WIN32)
    set(RUNTIME_DATA_OUTPUT_DIR "${APP_RESOURCES_OUTPUT_DIR}/.Panda")
    # The notices of the engine (its license and the third-party ones) ship with every exported game
    # (older SDKs have none).
    set(PANDA_NOTICES_COPY_COMMANDS "")
    if(EXISTS "${PANDA_SDK_ROOT}/THIRD_PARTY_NOTICES.txt")
        set(PANDA_NOTICES_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PANDA_SDK_ROOT}/THIRD_PARTY_NOTICES.txt"
                "${APP_RESOURCES_OUTPUT_DIR}/THIRD_PARTY_NOTICES.txt")
    endif()
    set(PANDA_UNLISTED_MODULE_RESOURCE_COMMANDS "")
    foreach(_PANDA_FOLDER ${PANDA_UNLISTED_MODULE_RESOURCE_FOLDERS})
        list(APPEND PANDA_UNLISTED_MODULE_RESOURCE_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${APP_RESOURCES_OUTPUT_DIR}/${_PANDA_FOLDER}")
    endforeach()
    add_custom_command(TARGET SmashLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${APP_RESOURCES_OUTPUT_DIR}/Content"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${RUNTIME_DATA_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PandaSDK_ENGINE_COMMON_RESOURCES_DIR}"
            "${APP_RESOURCES_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PandaSDK_ENGINE_SHADER_RESOURCES_DIR}"
            "${APP_RESOURCES_OUTPUT_DIR}"
        ${PANDA_NOTICES_COPY_COMMANDS}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNTIME_DATA_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PANDA_GAME_RUNTIME_DATA_DIR}"
            "${RUNTIME_DATA_OUTPUT_DIR}"
        ${PANDA_UNLISTED_MODULE_RESOURCE_COMMANDS}
        COMMENT "Copying cooked runtime data to app bundle"
    )
endif()

if(APPLE)
    add_custom_command(TARGET SmashLab POST_BUILD
        COMMAND /usr/bin/xattr -cr "$<TARGET_BUNDLE_DIR:SmashLab>"
        COMMENT "Removing extended attributes from app bundle"
    )
endif()

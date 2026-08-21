cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
endif()
project(VariousDialogueTags VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COMMONLIB_PREBUILT_MULTICONFIG ON CACHE BOOL "" FORCE)
add_subdirectory(lib/commonlibsse-ng EXCLUDE_FROM_ALL)
if(NOT COMMAND add_commonlibsse_plugin)
    include(lib/commonlibsse-ng/cmake/CommonLibSSE.cmake)
endif()

set(VDT_INTERNAL_DATA_PATH "${CMAKE_CURRENT_BINARY_DIR}/VariousDialogueTags_InternalData.ini")
set(VDT_RESOURCE_HEADER_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/Resource.h")
set(VDT_INTERNAL_DATA_PREMERGED_PATH "" CACHE FILEPATH
    "Optional premerged internal-data INI produced by CI")
set(VDT_SHARED_PARENT_BASE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/SharedParentList_Base.ini")
set(VDT_CURATED_DATA_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/VariousDialogueTags_Curated.ini")
set(VDT_MERGE_SCRIPT_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Merge-TagLayers.ps1")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${VDT_SHARED_PARENT_BASE_PATH}"
    "${VDT_CURATED_DATA_PATH}"
    "${VDT_MERGE_SCRIPT_PATH}"
)

if(VDT_INTERNAL_DATA_PREMERGED_PATH)
    if(NOT EXISTS "${VDT_INTERNAL_DATA_PREMERGED_PATH}")
        message(FATAL_ERROR
            "Premerged internal-data INI not found: ${VDT_INTERNAL_DATA_PREMERGED_PATH}")
    endif()
    configure_file(
        "${VDT_INTERNAL_DATA_PREMERGED_PATH}"
        "${VDT_INTERNAL_DATA_PATH}"
        COPYONLY
    )
else()
    find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
    execute_process(
        COMMAND "${POWERSHELL_EXECUTABLE}"
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${VDT_MERGE_SCRIPT_PATH}"
            -Product VDT
            -BasePath "${VDT_SHARED_PARENT_BASE_PATH}"
            -CuratedPath "${VDT_CURATED_DATA_PATH}"
            -OutputPath "${VDT_INTERNAL_DATA_PATH}"
        RESULT_VARIABLE VDT_MERGE_RESULT
        OUTPUT_VARIABLE VDT_MERGE_OUTPUT
        ERROR_VARIABLE VDT_MERGE_ERROR
    )
    if(NOT VDT_MERGE_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Failed to merge VDT internal data:\n${VDT_MERGE_OUTPUT}\n${VDT_MERGE_ERROR}")
    endif()
endif()
configure_file(
    src/VariousDialogueTags.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/VariousDialogueTags.rc"
    @ONLY
)

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME "VariousDialogueTags"
    AUTHOR "Various Dialogue Tags contributors"
    SOURCES
        src/main.cpp
        src/PCH.h
        src/Config.cpp
        src/Config.h
        src/Menu.cpp
        src/Menu.h
        src/EmbeddedData.cpp
        src/EmbeddedData.h
        src/DialogueMenuHook.cpp
        src/DialogueMenuHook.h
        src/DBVO2LegacyCompatibility.cpp
        src/DBVO2LegacyCompatibility.h
        src/FormOrigin.cpp
        src/FormOrigin.h
        src/Resource.h
        "${CMAKE_CURRENT_BINARY_DIR}/VariousDialogueTags.rc"
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "00_VariousDialogueTags")

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/skse-menu-framework/resources")

target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
option(VDT_PRECOMPILE_HEADERS "Use precompiled headers" ON)
if(VDT_PRECOMPILE_HEADERS)
    target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.h")
endif()
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION "SKSE/Plugins"
    LIBRARY DESTINATION "SKSE/Plugins"
)

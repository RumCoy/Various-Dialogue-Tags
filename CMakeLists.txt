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

set(VDT_INTERNAL_DATA_SOURCE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/zz. Mod Data - EDIT FILES HERE (Symlinked to MO2)/VariousDialogueTags_InternalData.ini")
set(VDT_INTERNAL_DATA_PATH "${CMAKE_CURRENT_BINARY_DIR}/VariousDialogueTags_InternalData.ini")
set(VDT_RESOURCE_HEADER_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/Resource.h")
configure_file("${VDT_INTERNAL_DATA_SOURCE_PATH}" "${VDT_INTERNAL_DATA_PATH}" COPYONLY)
configure_file(
    src/VariousDialogueTags.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/VariousDialogueTags.rc"
    @ONLY
)

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME "Various Dialogue Tags"
    AUTHOR "Various Dialogue Tags contributors"
    SOURCES
        src/main.cpp
        src/PCH.h
        src/Config.cpp
        src/Config.h
        src/EmbeddedData.cpp
        src/EmbeddedData.h
        src/DialogueMenuHook.cpp
        src/DialogueMenuHook.h
        src/FormOrigin.cpp
        src/FormOrigin.h
        src/Resource.h
        "${CMAKE_CURRENT_BINARY_DIR}/VariousDialogueTags.rc"
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "00_VariousDialogueTags")

target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION "SKSE/Plugins"
    LIBRARY DESTINATION "SKSE/Plugins"
)

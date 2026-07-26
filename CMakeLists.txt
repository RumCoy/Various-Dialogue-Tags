cmake_minimum_required(VERSION 3.24)
project(VariousDialogueTags VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CommonLibSSE CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME "Various Dialogue Tags"
    AUTHOR "Various Dialogue Tags contributors"
    SOURCES
        src/main.cpp
        src/PCH.h
        src/Config.cpp
        src/Config.h
        src/DialogueMenuHook.cpp
        src/DialogueMenuHook.h
        src/FormOrigin.cpp
        src/FormOrigin.h
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
install(FILES "zz. Mod Data - EDIT FILES HERE (Symlinked to MO2)/Data/SKSE/Plugins/VariousDialogueTags_InternalData.ini" DESTINATION "SKSE/Plugins")

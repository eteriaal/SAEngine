# SAE - simple audio engine without specialization
# eteriaal 2025-2026 https://github.com/eteriaal/SAEngine

cmake_minimum_required(VERSION 3.28...4.2) 
project(SAEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# find include & lib
if(WIN32)
    find_package(FFMPEG REQUIRED COMPONENTS avcodec avformat avutil swresample)
    find_package(OpenAL REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswresample)
    pkg_check_modules(OPENAL REQUIRED IMPORTED_TARGET openal)
endif()

# SAE sources
add_library(${PROJECT_NAME} STATIC
    SAEngine.cpp
    SAEngine.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# target include & link lib
if(WIN32)
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${FFMPEG_INCLUDE_DIRS}
        ${OPENAL_INCLUDE_DIR}
    )
    target_link_libraries(${PROJECT_NAME} PUBLIC
        ${FFMPEG_LIBRARIES}
        OpenAL::OpenAL
    )
else()
    target_link_libraries(${PROJECT_NAME} PUBLIC
        PkgConfig::FFMPEG
        PkgConfig::OPENAL
    )
endif()

# optional demo
option(SAENGINE_BUILD_EXAMPLE "Build example executable" OFF)

if(SAENGINE_BUILD_EXAMPLE)
    add_executable(${PROJECT_NAME}_example
        main.cpp
    )
    target_link_libraries(${PROJECT_NAME}_example PRIVATE ${PROJECT_NAME})

endif()
cmake_minimum_required(VERSION 3.5)

project(ffmpeg-test)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoReader.cpp
)

include_directories(
    /opt/homebrew/include
)

find_library(AVFORMAT_LIBRARY avformat)
find_library(AVUTIL_LIBRARY avutil)

target_link_libraries(test PUBLIC ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreFoundation")

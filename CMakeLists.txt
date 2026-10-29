cmake_minimum_required(VERSION 3.28)

if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architecture")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version")
endif()

project(jtimeline VERSION 0.1.0 LANGUAGES C CXX)

if(MSVC)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(JTIMELINE_BUILD_TESTS "Build unit tests" ON)
option(JTIMELINE_BUILD_DEMO "Build the demo app" ON)
option(JTIMELINE_BUILD_MODULE_CHECK "Build a program that uses the modules through juce_add_module" OFF)
option(JTIMELINE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(JTIMELINE_CLANG_TIDY "Run clang-tidy while compiling" OFF)
set(JTIMELINE_SANITIZE "" CACHE STRING "Sanitizers to enable: address, undefined, thread (comma separated)")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(JtimelineOptions)
include(JtimelineDependencies)

if(JTIMELINE_CLANG_TIDY)
    find_program(JTIMELINE_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
endif()

add_library(jwaveform_static STATIC
    modules/jwaveform/core/jwaveform_PeakBuilder.cpp
    modules/jwaveform/core/jwaveform_PeakCache.cpp
    modules/jwaveform/core/jwaveform_PeakColumns.cpp
    modules/jwaveform/core/jwaveform_PeakData.cpp
    modules/jwaveform/core/jwaveform_PeakLevels.cpp
    modules/jwaveform/view/jwaveform_WaveformDrawing.cpp
    modules/jwaveform/view/jwaveform_WaveformView.cpp)
add_library(jtimeline::jwaveform ALIAS jwaveform_static)
target_include_directories(jwaveform_static PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules")
target_link_libraries(jwaveform_static PUBLIC jtimeline::juce)
jtimeline_configure_target(jwaveform_static)

add_library(jtimeline_static STATIC
    modules/jtimeline/core/jtimeline_TimeMapping.cpp
    modules/jtimeline/core/jtimeline_WheelInput.cpp)
add_library(jtimeline::jtimeline ALIAS jtimeline_static)
target_link_libraries(jtimeline_static PUBLIC jtimeline::jwaveform)
jtimeline_configure_target(jtimeline_static)

if(JTIMELINE_BUILD_DEMO)
    add_subdirectory(demo)
endif()

if(JTIMELINE_BUILD_TESTS OR JTIMELINE_BUILD_MODULE_CHECK)
    enable_testing()
endif()

if(JTIMELINE_BUILD_MODULE_CHECK)
    add_subdirectory(tests/module)
endif()

if(JTIMELINE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

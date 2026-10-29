include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE
    SYSTEM)
FetchContent_MakeAvailable(JUCE)

add_library(jtimeline_juce STATIC)
add_library(jtimeline::juce ALIAS jtimeline_juce)
target_link_libraries(jtimeline_juce
    PRIVATE
        juce::juce_audio_formats
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags)
target_compile_definitions(jtimeline_juce PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_include_directories(jtimeline_juce SYSTEM INTERFACE
    $<TARGET_PROPERTY:jtimeline_juce,INCLUDE_DIRECTORIES>)
target_compile_definitions(jtimeline_juce INTERFACE
    $<TARGET_PROPERTY:jtimeline_juce,COMPILE_DEFINITIONS>)
set_target_properties(jtimeline_juce PROPERTIES CXX_STANDARD 20)
jtimeline_enable_sanitizers(jtimeline_juce)

if(JTIMELINE_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.16.1
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    jtimeline_enable_sanitizers(Catch2)
    jtimeline_enable_sanitizers(Catch2WithMain)
endif()

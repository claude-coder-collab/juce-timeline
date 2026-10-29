juce_add_module("${PROJECT_SOURCE_DIR}/modules/jwaveform")
juce_add_module("${PROJECT_SOURCE_DIR}/modules/jtimeline")

juce_add_console_app(jtimeline_module_check PRODUCT_NAME "JUCE Timeline Module Check")
target_sources(jtimeline_module_check PRIVATE Main.cpp)
target_compile_definitions(jtimeline_module_check PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(jtimeline_module_check PRIVATE jtimeline jwaveform juce::juce_recommended_config_flags)
add_test(NAME module_check COMMAND jtimeline_module_check)

juce_add_gui_app(jwaveform_demo
    PRODUCT_NAME "JUCE Waveform Demo"
    COMPANY_NAME "jtimeline"
    BUNDLE_ID "dev.jtimeline.waveformdemo"
    VERSION ${PROJECT_VERSION})
target_sources(jwaveform_demo PRIVATE Main.cpp WaveformDemo.cpp)
target_link_libraries(jwaveform_demo PRIVATE jtimeline::jtimeline)
target_compile_definitions(jwaveform_demo PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:jwaveform_demo,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:jwaveform_demo,JUCE_VERSION>")
jtimeline_configure_target(jwaveform_demo)

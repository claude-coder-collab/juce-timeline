juce_add_gui_app(jtimeline_demo
    PRODUCT_NAME "JUCE Timeline Demo"
    COMPANY_NAME "jtimeline"
    BUNDLE_ID "dev.jtimeline.demo"
    VERSION ${PROJECT_VERSION})
target_sources(jtimeline_demo PRIVATE Main.cpp DemoModel.cpp TimelineDemo.cpp)
target_link_libraries(jtimeline_demo PRIVATE jtimeline::jtimeline)
target_include_directories(jtimeline_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shared)
target_compile_definitions(jtimeline_demo PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:jtimeline_demo,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:jtimeline_demo,JUCE_VERSION>")
jtimeline_configure_target(jtimeline_demo)

include(Catch)

add_executable(jtimeline_tests
    core/test_ClipTiming.cpp
    core/test_KeyInput.cpp
    core/test_PeakLevels.cpp
    core/test_RulerTicks.cpp
    core/test_SampleRange.cpp
    core/test_TimeMapping.cpp
    core/test_TrackLayout.cpp
    core/test_WheelInput.cpp
    core/test_ZoomLimits.cpp
    timeline/test_Timeline.cpp
    waveform/test_PeakBuilder.cpp
    waveform/test_PeakCache.cpp
    waveform/test_PeakColumns.cpp
    waveform/test_PeakConcurrency.cpp
    waveform/test_PeakData.cpp
    waveform/test_WaveformDrawing.cpp)
target_link_libraries(jtimeline_tests PRIVATE jtimeline::jtimeline Catch2::Catch2WithMain)
jtimeline_configure_target(jtimeline_tests)
catch_discover_tests(jtimeline_tests)

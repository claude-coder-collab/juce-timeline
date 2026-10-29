#pragma once

#include "DemoModel.h"

class TimelineDemo final : public juce::Component, private juce::Timer
{
public:
    TimelineDemo();
    ~TimelineDemo() override;

    TimelineDemo(const TimelineDemo&) = delete;
    TimelineDemo& operator=(const TimelineDemo&) = delete;
    TimelineDemo(TimelineDemo&&) = delete;
    TimelineDemo& operator=(TimelineDemo&&) = delete;

    void resized() override;
    void applyArguments(const juce::StringArray& args);

private:
    void timerCallback() override;
    void updateStatus();
    void selectScenario();
    void selectFormat();

    DemoModel model;
    jtimeline::Timeline timeline;
    jtimeline::Timeline overview;
    juce::ComboBox scenario;
    juce::ComboBox format;
    juce::TextButton fit{ "Zoom to fit" };
    juce::TextButton play{ "Play" };
    juce::ToggleButton follow{ "Page follow" };
    juce::TextButton taller{ "Taller" };
    juce::TextButton shorter{ "Shorter" };
    juce::TextButton edit{ "Edit 20 clips" };
    juce::TextButton addTrack{ "Add track" };
    juce::TextButton removeTrack{ "Remove track" };
    juce::Label hint;
    juce::Label status;
    double playStartTime = 0.0;
    jtimeline::SamplePosition playStartPosition = 0;
};

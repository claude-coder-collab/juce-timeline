#pragma once

#include "DemoModel.h"

#include <memory>

class TimelineDemo final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
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
    [[nodiscard]] bool isIdle() const;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void updateStatus();
    void selectScenario();
    void selectFormat();
    void addFiles(const juce::Array<juce::File>& files);

    DemoModel model;
    std::shared_ptr<jwaveform::PeakCache> cache;
    std::unique_ptr<juce::FileChooser> chooser;
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
    juce::TextButton addFilesButton{ "Add files..." };
    juce::ToggleButton slowBuilds{ "Slow builds" };
    juce::Label hint;
    juce::Label status;
    double playStartTime = 0.0;
    jtimeline::SamplePosition playStartPosition = 0;
};

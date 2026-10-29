#pragma once

#include <jtimeline/jtimeline.h>

#include <memory>

class WaveformDemo final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    WaveformDemo();
    ~WaveformDemo() override;

    WaveformDemo(const WaveformDemo&) = delete;
    WaveformDemo& operator=(const WaveformDemo&) = delete;
    WaveformDemo(WaveformDemo&&) = delete;
    WaveformDemo& operator=(WaveformDemo&&) = delete;

    [[nodiscard]] bool isIdle() const;

    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    class Overview;

    void timerCallback() override;
    void openFile(const juce::File& file);
    void openSynthetic();
    void setSource(const jwaveform::WaveformSource& source);
    void setMapping(const jtimeline::TimeMapping& mapping);
    void centreOn(double sourcePosition);
    void updateStyle();
    [[nodiscard]] double sourceLength() const;
    [[nodiscard]] double sampleRate() const;
    [[nodiscard]] jtimeline::ZoomLimits zoomLimits() const;

    std::shared_ptr<jwaveform::PeakCache> cache;
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::FileChooser> chooser;
    jtimeline::TimeMapping mapping;
    int syntheticCount = 0;

    juce::TextButton openButton{ "Open file..." };
    juce::TextButton syntheticButton{ "Synthetic 1 h" };
    juce::ToggleButton slowBuild{ "Slow build" };
    juce::ComboBox channelMode;
    juce::Slider gain;
    juce::Slider fadeIn;
    juce::Slider fadeOut;
    juce::Slider displayScale;
    juce::Label gainLabel{ {}, "Gain dB" };
    juce::Label fadeInLabel{ {}, "Fade in s" };
    juce::Label fadeOutLabel{ {}, "Fade out s" };
    juce::Label scaleLabel{ {}, "Waveform zoom" };
    jwaveform::WaveformView mainView;
    std::unique_ptr<Overview> overview;
    juce::Label hint;
    juce::Label status;
};

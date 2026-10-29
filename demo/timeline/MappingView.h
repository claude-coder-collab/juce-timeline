#pragma once

#include <jtimeline/jtimeline.h>

#include <functional>
#include <vector>

class MappingView final : public juce::Component
{
public:
    explicit MappingView(std::function<void()> onViewChanged);

    [[nodiscard]] const jtimeline::TimeMapping& getMapping() const { return mapping; }
    [[nodiscard]] jtimeline::SamplePosition getPlayhead() const { return playhead; }
    [[nodiscard]] double getWaveformScale() const { return waveformScale; }
    [[nodiscard]] static double getSampleRate() { return sampleRate; }
    void zoomToFit();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;

private:
    void setMapping(const jtimeline::TimeMapping& newMapping);
    void changed();
    [[nodiscard]] double clampedTrackScroll(double value) const;
    [[nodiscard]] double clipAreaWidth() const;
    [[nodiscard]] jtimeline::ZoomLimits zoomLimits() const;

    static constexpr double sampleRate = 48000.0;
    static constexpr jtimeline::SamplePosition timelineLength = 48000LL * 3600;
    static constexpr int laneCount = 16;
    static constexpr int laneHeight = 56;

    std::function<void()> onViewChanged;
    std::vector<std::vector<jtimeline::ClipTiming>> lanes;
    jtimeline::TimeMapping mapping;
    jtimeline::SamplePosition playhead = 0;
    double waveformScale = 1.0;
    double trackScroll = 0.0;
    bool fitted = false;
};

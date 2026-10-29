#pragma once

#include <jtimeline/jtimeline.h>

#include <functional>
#include <vector>

class MappingView final : public juce::Component
{
public:
    explicit MappingView(std::function<void()> onMappingChanged);

    [[nodiscard]] const jtimeline::TimeMapping& getMapping() const { return mapping; }
    void zoomToFit();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;

private:
    void setMapping(const jtimeline::TimeMapping& newMapping);
    void zoomBy(double factor, double anchorPixel);
    [[nodiscard]] jtimeline::ZoomLimits zoomLimits() const;

    std::function<void()> onMappingChanged;
    static constexpr double sampleRate = 48000.0;
    static constexpr jtimeline::SamplePosition timelineLength = 48000LL * 3600;
    jtimeline::TimeMapping mapping;
    bool fitted = false;
    std::vector<jtimeline::ClipTiming> clips;
};

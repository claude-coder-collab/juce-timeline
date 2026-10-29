#include "MappingView.h"

#include <cmath>
#include <utility>

namespace
{

constexpr float rulerHeight = 24.0f;
constexpr float laneHeight = 40.0f;

jtimeline::SamplePosition tickSpacingSamples(double samplesPerPixel)
{
    const auto minimum = std::max(1.0, samplesPerPixel * 80.0);
    for (jtimeline::SamplePosition decade = 1;; decade *= 10)
        for (const jtimeline::SamplePosition step : { 1, 2, 5 })
            if (static_cast<double>(decade * step) >= minimum)
                return decade * step;
}

}

MappingView::MappingView(std::function<void()> onMappingChangedIn) :
    onMappingChanged(std::move(onMappingChangedIn))
{
    juce::Random random{ 42 };
    for (jtimeline::SamplePosition start = 0; start < timelineLength;)
    {
        const auto length = static_cast<jtimeline::SamplePosition>(sampleRate * (0.01 + (random.nextDouble() * 20.0)));
        clips.push_back({ .start = start, .length = std::min(length, timelineLength - start), .sourceOffset = 0 });
        start += length + static_cast<jtimeline::SamplePosition>(sampleRate * random.nextDouble() * 5.0);
    }
}

jtimeline::ZoomLimits MappingView::zoomLimits() const
{
    return jtimeline::ZoomLimits::forTimeline(sampleRate, timelineLength, getWidth());
}

void MappingView::setMapping(const jtimeline::TimeMapping& newMapping)
{
    mapping = newMapping;
    repaint();
    if (onMappingChanged)
        onMappingChanged();
}

void MappingView::zoomToFit()
{
    setMapping(jtimeline::TimeMapping{ zoomLimits().maxSamplesPerPixel, 0 });
}

void MappingView::zoomBy(double factor, double anchorPixel)
{
    const auto target = zoomLimits().clamp(mapping.getSamplesPerPixel() * factor);
    setMapping(mapping.zoomedAround(target, anchorPixel));
}

void MappingView::resized()
{
    if (!fitted)
        zoomToFit();
    fitted = true;
}

void MappingView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
    if (event.mods.isCommandDown())
    {
        zoomBy(std::pow(2.0, -static_cast<double>(delta) * 4.0), static_cast<double>(event.position.x));
        return;
    }
    const auto horizontal = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
    setMapping(mapping.scrolledBy(-static_cast<double>(horizontal) * 200.0));
}

void MappingView::mouseMagnify(const juce::MouseEvent& event, float scaleFactor)
{
    if (scaleFactor > 0.0f)
        zoomBy(1.0 / static_cast<double>(scaleFactor), static_cast<double>(event.position.x));
}

void MappingView::paint(juce::Graphics& g)
{
    const auto width = static_cast<double>(getWidth());
    g.fillAll(juce::Colour{ 0xff1e1f22 });

    g.setColour(juce::Colour{ 0xff2b2d31 });
    g.fillRect(0.0f, 0.0f, static_cast<float>(width), rulerHeight);

    const auto visible = mapping.visibleRange(width);
    const auto spacing = tickSpacingSamples(mapping.getSamplesPerPixel());
    g.setFont(11.0f);
    const auto firstTick = visible.start >= 0 ? visible.start / spacing : -((-visible.start + spacing - 1) / spacing);
    for (auto position = firstTick * spacing; position < visible.end; position += spacing)
    {
        const auto x = mapping.toGraphicsX(position, width);
        g.setColour(juce::Colour{ 0xff5c6370 });
        g.drawVerticalLine(juce::roundToInt(x), 0.0f, static_cast<float>(getHeight()));
        g.setColour(juce::Colours::lightgrey);
        g.drawText(juce::String{ static_cast<double>(position) / sampleRate, 4 } + " s", juce::Rectangle{ x + 3.0f, 0.0f, 90.0f, rulerHeight }, juce::Justification::centredLeft);
    }

    const auto top = rulerHeight + 10.0f;
    for (const auto& clip : clips)
    {
        if (!clip.timelineRange().intersects(visible))
            continue;
        const auto left = mapping.toGraphicsX(clip.start, width);
        const auto right = mapping.toGraphicsX(clip.end(), width);
        g.setColour(juce::Colour{ 0xff3d8fd1 }.withAlpha(0.8f));
        g.fillRect(juce::Rectangle<float>::leftTopRightBottom(left, top, std::max(right, left + 1.0f), top + laneHeight));
    }

    if (mapping.getPixelsPerSample() >= 4.0)
    {
        g.setColour(juce::Colours::orange);
        for (auto sample = visible.start; sample < visible.end; ++sample)
            g.fillEllipse(mapping.toGraphicsX(sample, width) - 2.0f, top + laneHeight + 20.0f, 4.0f, 4.0f);
    }
}

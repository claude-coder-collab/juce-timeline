#include "MappingView.h"

#include <cmath>
#include <utility>

namespace
{

constexpr float rulerHeight = 24.0f;

jtimeline::SamplePosition tickSpacingSamples(double samplesPerPixel)
{
    const auto minimum = std::max(1.0, samplesPerPixel * 80.0);
    for (jtimeline::SamplePosition decade = 1;; decade *= 10)
        for (const jtimeline::SamplePosition step : { 1, 2, 5 })
            if (static_cast<double>(decade * step) >= minimum)
                return decade * step;
}

jtimeline::SamplePosition floorDiv(jtimeline::SamplePosition value, jtimeline::SamplePosition divisor)
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

}

MappingView::MappingView(std::function<void()> onViewChangedIn) :
    onViewChanged(std::move(onViewChangedIn))
{
    setWantsKeyboardFocus(true);
    juce::Random random{ 42 };
    lanes.resize(laneCount);
    for (auto& lane : lanes)
    {
        for (jtimeline::SamplePosition start = 0; start < timelineLength;)
        {
            const auto length = static_cast<jtimeline::SamplePosition>(sampleRate * (0.01 + (random.nextDouble() * 20.0)));
            lane.push_back({ .start = start, .length = std::min(length, timelineLength - start), .sourceOffset = 0 });
            start += length + static_cast<jtimeline::SamplePosition>(sampleRate * random.nextDouble() * 5.0);
        }
    }
}

double MappingView::clipAreaWidth() const
{
    return static_cast<double>(getWidth());
}

jtimeline::ZoomLimits MappingView::zoomLimits() const
{
    return jtimeline::ZoomLimits::forTimeline(sampleRate, timelineLength, clipAreaWidth());
}

void MappingView::changed()
{
    repaint();
    if (onViewChanged)
        onViewChanged();
}

void MappingView::setMapping(const jtimeline::TimeMapping& newMapping)
{
    mapping = newMapping;
    changed();
}

void MappingView::zoomToFit()
{
    setMapping(jtimeline::TimeMapping{ zoomLimits().maxSamplesPerPixel, 0 });
}

double MappingView::clampedTrackScroll(double value) const
{
    const auto visibleHeight = static_cast<double>(getHeight()) - static_cast<double>(rulerHeight);
    return std::clamp(value, 0.0, std::max(0.0, static_cast<double>(laneCount * laneHeight) - visibleHeight));
}

void MappingView::resized()
{
    if (!fitted)
        zoomToFit();
    fitted = true;
    trackScroll = clampedTrackScroll(trackScroll);
}

bool MappingView::keyPressed(const juce::KeyPress& key)
{
    const auto zoomAtPlayhead = [this](double steps) {
        const auto target = zoomLimits().clamp(jtimeline::wheel::zoomedSamplesPerPixel(mapping.getSamplesPerPixel(), steps, jtimeline::wheel::zoomFactorPerKey));
        setMapping(mapping.zoomedAroundSample(target, playhead, clipAreaWidth()));
    };
    const auto scaleWaveforms = [this](double steps) {
        waveformScale = std::clamp(waveformScale * std::pow(jtimeline::wheel::waveformFactorPerStep, steps), 0.25, 64.0);
        changed();
    };
    switch (jtimeline::keyActionFor(key))
    {
        case jtimeline::KeyAction::zoomIn:
            zoomAtPlayhead(1.0);
            return true;
        case jtimeline::KeyAction::zoomOut:
            zoomAtPlayhead(-1.0);
            return true;
        case jtimeline::KeyAction::zoomToFit:
            zoomToFit();
            return true;
        case jtimeline::KeyAction::waveformTaller:
            scaleWaveforms(1.0);
            return true;
        case jtimeline::KeyAction::waveformShorter:
            scaleWaveforms(-1.0);
            return true;
        case jtimeline::KeyAction::none:
        case jtimeline::KeyAction::trackTaller:
        case jtimeline::KeyAction::trackShorter:
        case jtimeline::KeyAction::selectAll:
            break;
    }
    return false;
}

void MappingView::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    playhead = std::clamp<jtimeline::SamplePosition>(mapping.nearestSampleTo(static_cast<double>(event.position.x)), 0, timelineLength);
    changed();
}

void MappingView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto action = jtimeline::wheel::actionFor(jtimeline::wheelModifiersFrom(event.mods), jtimeline::wheelDeltaFrom(wheel));

    const auto limits = zoomLimits();
    auto next = mapping;
    if (action.scrollTimePixels != 0.0)
        next = next.scrolledBy(action.scrollTimePixels);
    if (action.zoomAtPointerSteps != 0.0)
        next = next.zoomedAround(limits.clamp(jtimeline::wheel::zoomedSamplesPerPixel(next.getSamplesPerPixel(), action.zoomAtPointerSteps, jtimeline::wheel::zoomFactorPerNotch)), static_cast<double>(event.position.x));
    if (action.zoomAtEditCursorSteps != 0.0)
        next = next.zoomedAroundSample(limits.clamp(jtimeline::wheel::zoomedSamplesPerPixel(next.getSamplesPerPixel(), action.zoomAtEditCursorSteps, jtimeline::wheel::zoomFactorPerNotch)), playhead, clipAreaWidth());
    if (action.waveformZoomSteps != 0.0)
        waveformScale = std::clamp(waveformScale * std::pow(jtimeline::wheel::waveformFactorPerStep, action.waveformZoomSteps), 0.25, 64.0);
    if (action.scrollTracksPixels != 0.0)
        trackScroll = clampedTrackScroll(trackScroll + action.scrollTracksPixels);
    setMapping(next);
}

void MappingView::mouseMagnify(const juce::MouseEvent& event, float scaleFactor)
{
    if (scaleFactor > 0.0f)
        setMapping(mapping.zoomedAround(zoomLimits().clamp(mapping.getSamplesPerPixel() / static_cast<double>(scaleFactor)), static_cast<double>(event.position.x)));
}

void MappingView::paint(juce::Graphics& g)
{
    const auto width = clipAreaWidth();
    const auto height = static_cast<float>(getHeight());
    const auto visible = mapping.visibleRange(width);
    g.fillAll(juce::Colour{ 0xff1e1f22 });

    {
        const juce::Graphics::ScopedSaveState state{ g };
        g.reduceClipRegion(juce::Rectangle{ 0, static_cast<int>(rulerHeight), getWidth(), getHeight() });
        const auto firstLane = static_cast<int>(trackScroll) / laneHeight;
        for (auto index = firstLane; index < laneCount; ++index)
        {
            const auto top = rulerHeight + static_cast<float>((index * laneHeight) - trackScroll);
            if (top > height)
                break;
            g.setColour(juce::Colour{ 0xff2b2d31 });
            g.drawHorizontalLine(juce::roundToInt(top + static_cast<float>(laneHeight) - 1.0f), 0.0f, static_cast<float>(width));
            const auto clipTop = top + 4.0f;
            const auto clipHeight = static_cast<float>(laneHeight) - 8.0f;
            const auto band = std::min(clipHeight, static_cast<float>(static_cast<double>(clipHeight) * 0.2 * waveformScale));
            for (const auto& clip : lanes[static_cast<std::size_t>(index)])
            {
                if (!clip.timelineRange().intersects(visible))
                    continue;
                const auto left = mapping.toGraphicsX(clip.start, width);
                const auto right = std::max(mapping.toGraphicsX(clip.end(), width), left + 1.0f);
                g.setColour(juce::Colour{ 0xff3d8fd1 }.withAlpha(0.8f));
                g.fillRect(juce::Rectangle<float>::leftTopRightBottom(left, clipTop, right, clipTop + clipHeight));
                g.setColour(juce::Colour{ 0xffcfe6fa });
                g.fillRect(juce::Rectangle<float>::leftTopRightBottom(left, clipTop + ((clipHeight - band) / 2.0f), right, clipTop + ((clipHeight + band) / 2.0f)));
            }
        }

        if (mapping.getPixelsPerSample() >= 4.0)
        {
            g.setColour(juce::Colours::orange);
            for (auto sample = visible.start; sample < visible.end; ++sample)
                g.fillEllipse(mapping.toGraphicsX(sample, width) - 2.0f, rulerHeight + 2.0f, 4.0f, 4.0f);
        }
    }

    g.setColour(juce::Colour{ 0xff2b2d31 });
    g.fillRect(0.0f, 0.0f, static_cast<float>(width), rulerHeight);
    const auto spacing = tickSpacingSamples(mapping.getSamplesPerPixel());
    g.setFont(11.0f);
    for (auto position = floorDiv(visible.start, spacing) * spacing; position < visible.end; position += spacing)
    {
        const auto x = mapping.toGraphicsX(position, width);
        g.setColour(juce::Colour{ 0xff5c6370 });
        g.drawVerticalLine(juce::roundToInt(x), 0.0f, rulerHeight);
        g.setColour(juce::Colours::lightgrey);
        g.drawText(juce::String{ static_cast<double>(position) / sampleRate, 4 } + " s", juce::Rectangle{ x + 3.0f, 0.0f, 90.0f, rulerHeight }, juce::Justification::centredLeft);
    }

    if (visible.contains(playhead))
    {
        g.setColour(juce::Colours::white);
        g.drawVerticalLine(juce::roundToInt(mapping.toGraphicsX(playhead, width)), 0.0f, height);
    }
}

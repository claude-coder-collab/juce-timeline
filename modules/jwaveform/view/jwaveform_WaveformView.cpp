#include "jwaveform_WaveformView.h"

#include <utility>

namespace jwaveform
{

WaveformView::WaveformView()
{
    setOpaque(false);
}

WaveformView::~WaveformView() = default;

void WaveformView::setPeakCache(std::shared_ptr<PeakCache> newCache)
{
    cache = std::move(newCache);
    request();
}

void WaveformView::setSource(const WaveformSource& newSource)
{
    source = newSource;
    request();
}

void WaveformView::clearSource()
{
    setSource({});
}

void WaveformView::request()
{
    handle = {};
    if (source.peaks != nullptr)
    {
        handle = PeakHandle::forPeaks(source.peaks);
    }
    else if (cache != nullptr)
    {
        handle = cache->request(source, [safe = juce::Component::SafePointer{ this }] {
            if (safe != nullptr)
                safe->repaint();
        });
    }
    repaint();
}

void WaveformView::setVisibleRange(double sourceStart, double sourceEnd)
{
    visibleStart = sourceStart;
    visibleEnd = sourceEnd;
    repaint();
}

void WaveformView::setStyle(const WaveformStyle& newStyle)
{
    style = newStyle;
    repaint();
}

juce::Colour WaveformView::resolveColour(int colourId) const
{
    for (const auto* component = static_cast<const juce::Component*>(this); component != nullptr; component = component->getParentComponent())
        if (component->isColourSpecified(colourId))
            return component->findColour(colourId);
    if (getLookAndFeel().isColourSpecified(colourId))
        return getLookAndFeel().findColour(colourId);

    switch (colourId)
    {
        case backgroundColourId:
            return juce::Colours::transparentBlack;
        case waveformColourId:
            return findColour(juce::Slider::thumbColourId, true);
        case centreLineColourId:
            return findColour(juce::Label::textColourId, true).withAlpha(0.25f);
        case textColourId:
        default:
            return findColour(juce::Label::textColourId, true);
    }
}

void WaveformView::paint(juce::Graphics& g)
{
    g.fillAll(resolveColour(backgroundColourId));
    const auto data = handle.getData();
    const auto state = handle.getState();

    juce::String message;
    if (state == PeakState::cannotOpen)
        message = "Can't open the audio";
    else if (state == PeakState::readError && data == nullptr)
        message = "Can't read the audio";
    else if (state == PeakState::loading && data == nullptr)
        message = "Loading...";

    if (data != nullptr)
    {
        auto drawStyle = style;
        drawStyle.colour = resolveColour(waveformColourId);
        drawStyle.centreLineColour = resolveColour(centreLineColourId);
        const auto whole = !(visibleEnd > visibleStart);
        drawWaveform(g, getLocalBounds().toFloat(), *data, whole ? 0.0 : visibleStart, whole ? static_cast<double>(data->getLength()) : visibleEnd, drawStyle);
    }

    if (message.isNotEmpty())
    {
        g.setColour(resolveColour(textColourId));
        g.drawText(message, getLocalBounds(), juce::Justification::centred);
    }
}

}

#include "jtimeline_TimeMapping.h"

#include <algorithm>
#include <cmath>

namespace jtimeline
{

namespace
{

double sanitisedSamplesPerPixel(double value)
{
    return std::isfinite(value) && value > 0.0 ? value : 1.0;
}

}

TimeMapping::TimeMapping(double samplesPerPixelIn, SamplePosition startSampleIn, double startFractionIn) :
    samplesPerPixel(sanitisedSamplesPerPixel(samplesPerPixelIn))
{
    *this = withStartOffsetFrom(startSampleIn, std::isfinite(startFractionIn) ? startFractionIn : 0.0);
}

TimeMapping TimeMapping::withStartOffsetFrom(SamplePosition origin, double offset) const
{
    const auto whole = std::floor(offset);
    auto result = *this;
    result.startSample = origin + static_cast<SamplePosition>(whole);
    result.startFraction = offset - whole;
    if (result.startFraction >= 1.0)
    {
        ++result.startSample;
        result.startFraction = 0.0;
    }
    return result;
}

double TimeMapping::toPixel(SamplePosition position) const
{
    return (static_cast<double>(position - startSample) - startFraction) / samplesPerPixel;
}

double TimeMapping::toPixelLength(SamplePosition length) const
{
    return static_cast<double>(length) / samplesPerPixel;
}

SamplePosition TimeMapping::sampleAt(double pixel) const
{
    return startSample + static_cast<SamplePosition>(std::floor(startFraction + (pixel * samplesPerPixel)));
}

SamplePosition TimeMapping::nearestSampleTo(double pixel) const
{
    return startSample + static_cast<SamplePosition>(std::floor(startFraction + (pixel * samplesPerPixel) + 0.5));
}

float TimeMapping::toGraphicsX(SamplePosition position, double width, double margin) const
{
    return static_cast<float>(std::clamp(toPixel(position), -margin, std::max(width, 0.0) + margin));
}

SampleRange TimeMapping::visibleRange(double width) const
{
    if (!(width > 0.0))
        return { .start = startSample, .end = startSample };
    const auto end = startSample + static_cast<SamplePosition>(std::ceil(startFraction + (width * samplesPerPixel)));
    return { .start = startSample, .end = end };
}

TimeMapping TimeMapping::zoomedAround(double newSamplesPerPixel, double anchorPixel) const
{
    const auto anchorOffset = startFraction + (anchorPixel * samplesPerPixel);
    const auto anchorWhole = std::floor(anchorOffset);
    const auto anchorSample = startSample + static_cast<SamplePosition>(anchorWhole);
    auto result = *this;
    result.samplesPerPixel = sanitisedSamplesPerPixel(newSamplesPerPixel);
    return result.withStartOffsetFrom(anchorSample, (anchorOffset - anchorWhole) - (anchorPixel * result.samplesPerPixel));
}

TimeMapping TimeMapping::zoomedAroundSample(double newSamplesPerPixel, SamplePosition anchor, double width) const
{
    const auto pixel = toPixel(anchor);
    const auto inView = pixel >= 0.0 && pixel <= width;
    auto result = *this;
    result.samplesPerPixel = sanitisedSamplesPerPixel(newSamplesPerPixel);
    return result.withSampleAt(anchor, inView ? pixel : std::max(width, 0.0) / 2.0);
}

TimeMapping TimeMapping::scrolledBy(double pixels) const
{
    return withStartOffsetFrom(startSample, startFraction + (pixels * samplesPerPixel));
}

TimeMapping TimeMapping::withSampleAt(SamplePosition position, double pixel) const
{
    return withStartOffsetFrom(position, -pixel * samplesPerPixel);
}

ZoomLimits ZoomLimits::forTimeline(double sampleRate, SamplePosition length, double viewWidth, double minSamplesPerPixel)
{
    const auto rate = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 1.0;
    const auto minimum = sanitisedSamplesPerPixel(minSamplesPerPixel);
    const auto span = std::max(static_cast<double>(length), minimumTimelineSeconds * rate);
    const auto maximum = span / std::max(viewWidth, 1.0);
    return { .minSamplesPerPixel = minimum, .maxSamplesPerPixel = std::max(minimum, maximum) };
}

double ZoomLimits::clamp(double samplesPerPixel) const
{
    return std::clamp(sanitisedSamplesPerPixel(samplesPerPixel), minSamplesPerPixel, maxSamplesPerPixel);
}

}

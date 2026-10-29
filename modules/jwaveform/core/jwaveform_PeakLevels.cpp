#include "jwaveform_PeakLevels.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace jwaveform
{

namespace
{

SamplePosition ceilDiv(SamplePosition value, SamplePosition divisor)
{
    return (value + divisor - 1) / divisor;
}

}

PeakLevels::PeakLevels(SamplePosition sourceLengthIn, int samplesPerPeakIn) :
    sourceLength(std::max<SamplePosition>(0, sourceLengthIn)),
    samplesPerPeak(std::max(1, samplesPerPeakIn))
{
    while (pairCountAt(levelCount - 1) > 1)
        ++levelCount;
}

SamplePosition PeakLevels::samplesPerPairAt(int level) const
{
    assert(level >= 0 && level < 63);
    return SamplePosition{ samplesPerPeak } * static_cast<SamplePosition>(std::uint64_t{ 1 } << static_cast<unsigned>(level));
}

SamplePosition PeakLevels::pairCountAt(int level) const
{
    return ceilDiv(sourceLength, samplesPerPairAt(level));
}

SampleRange PeakLevels::pairsCovering(int level, SampleRange sourceRange) const
{
    const auto clamped = sourceRange.intersection({ .start = 0, .end = sourceLength });
    if (clamped.isEmpty())
        return {};
    const auto size = samplesPerPairAt(level);
    return { .start = clamped.start / size, .end = ceilDiv(clamped.end, size) };
}

int PeakLevels::levelFor(double samplesPerPhysicalPixel) const
{
    if (!(samplesPerPhysicalPixel > samplesPerPeak))
        return 0;
    const auto level = static_cast<int>(std::floor(std::log2(samplesPerPhysicalPixel / samplesPerPeak)));
    return std::clamp(level, 0, levelCount - 1);
}

}

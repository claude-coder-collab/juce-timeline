#pragma once

#include "jwaveform_SampleRange.h"

namespace jwaveform
{

/** The default number of source samples summarised by one level-0 min/max pair. */
inline constexpr int defaultSamplesPerPeak = 64;

//==============================================================================
/**
    The shape of a min/max peak pyramid for one source: level 0 holds one pair per
    `samplesPerPeak` samples, and each higher level halves the resolution, up to the first
    level that holds a single pair.
*/
class PeakLevels
{
public:
    /** Describes the pyramid for a source of the given length; a non-positive samplesPerPeak becomes 1. */
    explicit PeakLevels(SamplePosition sourceLength, int samplesPerPeak = defaultSamplesPerPeak);

    /** Returns the source length in samples. */
    [[nodiscard]] SamplePosition getSourceLength() const { return sourceLength; }
    /** Returns the number of source samples summarised by one level-0 pair. */
    [[nodiscard]] int getSamplesPerPeak() const { return samplesPerPeak; }
    /** Returns the number of levels; at least 1. */
    [[nodiscard]] int getLevelCount() const { return levelCount; }

    /** Returns the number of source samples summarised by one pair at the level. */
    [[nodiscard]] SamplePosition samplesPerPairAt(int level) const;
    /** Returns the number of pairs at the level, counting a final partial block. */
    [[nodiscard]] SamplePosition pairCountAt(int level) const;
    /** Returns the pairs at the level that cover the source range, clamped to the source. */
    [[nodiscard]] SampleRange pairsCovering(int level, SampleRange sourceRange) const;

    /**
        Returns the level to draw from when one physical pixel spans the given number of source
        samples: the coarsest level that still has at least one pair per pixel, so no detail
        finer than a pixel is lost. Returns 0 when even level 0 is coarser than a pixel.
    */
    [[nodiscard]] int levelFor(double samplesPerPhysicalPixel) const;

private:
    SamplePosition sourceLength = 0;
    int samplesPerPeak = defaultSamplesPerPeak;
    int levelCount = 1;
};

}

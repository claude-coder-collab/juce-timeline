#pragma once

#include "jwaveform_PeakColumns.h"

#include <memory>
#include <span>
#include <vector>

namespace jwaveform
{

//==============================================================================
/** A contiguous block of raw samples from one source, for every channel. */
class SampleBlock
{
public:
    /** Creates a zeroed block; a channel count below 1 becomes 1 and a negative frame count 0. */
    SampleBlock(SamplePosition start, int numChannels, int numFrames);

    /** Returns the source position of the first frame. */
    [[nodiscard]] SamplePosition getStart() const { return start; }
    /** Returns the number of frames. */
    [[nodiscard]] int getNumFrames() const { return numFrames; }
    /** Returns the number of channels. */
    [[nodiscard]] int getNumChannels() const { return numChannels; }
    /** Returns the source positions the block holds. */
    [[nodiscard]] SampleRange getRange() const { return { .start = start, .end = start + numFrames }; }
    /** Returns one channel's samples. */
    [[nodiscard]] std::span<const float> getChannel(int channel) const;
    /** Returns one channel's samples, for filling the block. */
    [[nodiscard]] std::span<float> getChannel(int channel);
    /** Returns the bytes the block holds. */
    [[nodiscard]] std::size_t getMemoryBytes() const { return samples.size() * sizeof(float); }

private:
    SamplePosition start = 0;
    int numChannels = 1;
    int numFrames = 0;
    std::vector<float> samples;
};

//==============================================================================
/**
    The raw samples available for a span of a source: the blocks covering it, some of which may
    still be missing. Cheap to copy; the blocks are shared.
*/
class RawSamples
{
public:
    /** Creates an empty set, holding no samples. */
    RawSamples() = default;
    /** Creates a set from blocks, each of which may be null; blocks must not overlap. */
    RawSamples(int numChannels, std::vector<std::shared_ptr<const SampleBlock>> blocks);

    /** Returns true if no samples are held. */
    [[nodiscard]] bool isEmpty() const { return blocks.empty(); }
    /** Returns the number of channels. */
    [[nodiscard]] int getNumChannels() const { return numChannels; }
    /** Returns true if every position of the range is held. */
    [[nodiscard]] bool contains(SampleRange range) const;
    /** Returns true if the position is held. */
    [[nodiscard]] bool contains(SamplePosition position) const { return blockFor(position) != nullptr; }
    /** Returns the sample at a held position; 0 for a position that is not held. */
    [[nodiscard]] float get(int channel, SamplePosition position) const;

private:
    [[nodiscard]] const SampleBlock* blockFor(SamplePosition position) const;

    int numChannels = 1;
    std::vector<std::shared_ptr<const SampleBlock>> blocks;
};

/** Returns true if drawing at this many source samples per physical pixel uses raw samples where they are held. */
[[nodiscard]] bool usesRawSamples(const PeakData& data, double samplesPerPhysicalPixel);

/** Returns the whole samples a fractional source span touches, plus one either side for joining lines. */
[[nodiscard]] SampleRange rawRangeFor(double sourceStart, double sourceEnd);

/**
    Fills the columns raw samples fully cover, as computeColumns() does from peaks: each column
    holds the range of the samples it overlaps, joined to the sample before it so consecutive
    columns connect like a line through the samples. Columns the samples do not fully cover are
    left unchanged, so they keep whatever peaks drew there.

    @param channel  a channel index, or allChannels to combine them
    @param sourceLength  the source's length; columns past it are left unchanged
*/
void computeRawColumns(const RawSamples& samples, int channel, SamplePosition sourceLength, double sourceStart, double samplesPerColumn, std::span<ColumnPeak> columns);

}

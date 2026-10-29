#pragma once

#include "jwaveform_PeakLevels.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace jwaveform
{

//==============================================================================
/** One min/max pair, with values normalised so that ±32767 is ±1. */
struct PeakPair
{
    std::int16_t min = 0; /**< The lowest value. */
    std::int16_t max = 0; /**< The highest value. */

    /** Returns the lowest value as a float in [-1, 1]. */
    [[nodiscard]] float minValue() const { return static_cast<float>(min) / 32767.0f; }
    /** Returns the highest value as a float in [-1, 1]. */
    [[nodiscard]] float maxValue() const { return static_cast<float>(max) / 32767.0f; }

    /** Compares two pairs. */
    friend bool operator==(const PeakPair&, const PeakPair&) = default;
};

//==============================================================================
/**
    A multi-resolution min/max pyramid for one audio source.

    Level 0 holds one pair per getLevels().getSamplesPerPeak() samples for each channel, and each
    higher level halves the resolution. Storage is allocated in fixed-size chunks that never move,
    and each level publishes how many of its pairs are valid, so one thread can read a partly
    built PeakData while another appends to it.

    Only one thread may write (append() and complete()). Any number may read concurrently; a reader
    must only read pairs below getValidPairs() for that level. Share it as
    std::shared_ptr<const PeakData> and keep the mutable pointer with the writer.
*/
class PeakData
{
public:
    /** The number of pairs per channel in one storage chunk. */
    static constexpr SamplePosition pairsPerChunk = 16384;

    /** Creates empty data for a source; a channel count below 1 becomes 1. */
    PeakData(double sampleRate, SamplePosition length, int numChannels, int samplesPerPeak = defaultSamplesPerPeak);
    /** Destructor. */
    ~PeakData();

    PeakData(const PeakData&) = delete;
    PeakData& operator=(const PeakData&) = delete;
    PeakData(PeakData&&) = delete;
    PeakData& operator=(PeakData&&) = delete;

    /** Returns the source's sample rate. */
    [[nodiscard]] double getSampleRate() const { return sampleRate; }
    /** Returns the source's length in samples. */
    [[nodiscard]] SamplePosition getLength() const { return levels.getSourceLength(); }
    /** Returns the number of channels. */
    [[nodiscard]] int getNumChannels() const { return numChannels; }
    /** Returns the shape of the pyramid. */
    [[nodiscard]] const PeakLevels& getLevels() const { return levels; }

    /** Returns how many pairs of the level may be read. Safe from any thread. */
    [[nodiscard]] SamplePosition getValidPairs(int level) const;
    /** Returns true once every level is fully valid. Safe from any thread. */
    [[nodiscard]] bool isComplete() const;
    /** Returns one pair; the index must be below getValidPairs(level). */
    [[nodiscard]] PeakPair getPair(int level, int channel, SamplePosition index) const;
    /** Returns the bytes of pair storage allocated so far. Safe from any thread. */
    [[nodiscard]] std::size_t getMemoryBytes() const;

    /** Writer only: returns the number of source frames appended so far. */
    [[nodiscard]] SamplePosition getFramesAppended() const { return framesAppended; }
    /**
        Writer only: appends frames of audio, one pointer per channel (at least getNumChannels()
        of them, each with numFrames samples).
        Frames past getLength() are ignored. New pairs are published before this returns.
    */
    void append(std::span<const float* const> channels, int numFrames);
    /**
        Writer only: finishes the data, writing any partial final block and every remaining
        higher-level pair, then marks it complete. Use it at the end of the source, even when
        the source turned out shorter than its stated length.
    */
    void complete();

private:
    struct Level;

    void emitLevel0Pair();
    [[nodiscard]] PeakPair getPairUnpublished(int level, int channel, SamplePosition index) const;
    void writePair(int level, SamplePosition index, int channel, PeakPair pair);
    void propagate(bool final);
    void publish();

    double sampleRate = 0.0;
    int numChannels = 1;
    PeakLevels levels;
    std::vector<std::unique_ptr<Level>> levelStorage;
    std::vector<float> blockMin;
    std::vector<float> blockMax;
    int framesInBlock = 0;
    SamplePosition framesAppended = 0;
    std::atomic<std::size_t> memoryBytes{ 0 };
    std::atomic<bool> completed{ false };
};

}

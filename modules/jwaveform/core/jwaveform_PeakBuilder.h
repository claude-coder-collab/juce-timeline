#pragma once

#include "jwaveform_PeakData.h"

#include <atomic>
#include <functional>
#include <memory>

namespace juce
{
class AudioFormatReader;
}

namespace jwaveform
{

//==============================================================================
/**
    Builds PeakData from an AudioFormatReader, progressively: pairs are published as each block
    is read, so the data can be drawn while the build continues. Run it on a background thread.

    A build resumes from PeakData::getFramesAppended(), so a cancelled build can be continued later
    with a new reader for the same source.
*/
class PeakBuilder
{
public:
    /** The outcome of run(). */
    enum class Result {
        complete, /**< Every frame was read and the data is complete. */
        cancelled, /**< The cancel flag was set; the data holds what was read so far. */
        readError /**< The reader failed; the data holds what was read before the failure and is not complete. */
    };

    /** The default number of frames read per block. */
    static constexpr int defaultBlockSize = 65536;

    /** Creates empty PeakData matching a reader's sample rate, length and channel count. */
    [[nodiscard]] static std::shared_ptr<PeakData> createDataFor(const juce::AudioFormatReader& reader, int samplesPerPeak = defaultSamplesPerPeak);

    /** Creates a builder that reads from reader into data; both must outlive it. */
    PeakBuilder(juce::AudioFormatReader& reader, PeakData& data, int blockSize = defaultBlockSize);

    /**
        Reads blocks until the data is complete, cancel becomes true, or the reader fails.
        onProgress, if set, is called on this thread after each block.
    */
    Result run(const std::atomic<bool>& cancel, const std::function<void()>& onProgress = {});

private:
    juce::AudioFormatReader& reader;
    PeakData& data;
    int blockSize;
};

}

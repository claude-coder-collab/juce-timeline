#include "jwaveform_PeakBuilder.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>

namespace jwaveform
{

std::shared_ptr<PeakData> PeakBuilder::createDataFor(const juce::AudioFormatReader& reader, int samplesPerPeak)
{
    return std::make_shared<PeakData>(reader.sampleRate, std::max<SamplePosition>(0, reader.lengthInSamples), static_cast<int>(reader.numChannels), samplesPerPeak);
}

PeakBuilder::PeakBuilder(juce::AudioFormatReader& readerIn, PeakData& dataIn, int blockSizeIn) :
    reader(readerIn),
    data(dataIn),
    blockSize(std::max(1, blockSizeIn))
{
}

PeakBuilder::Result PeakBuilder::run(const std::atomic<bool>& cancel, const std::function<void()>& onProgress)
{
    if (data.isComplete())
        return Result::complete;

    juce::AudioBuffer<float> buffer{ data.getNumChannels(), blockSize };
    const auto readerLength = std::min<SamplePosition>(data.getLength(), reader.lengthInSamples);
    while (data.getFramesAppended() < readerLength)
    {
        if (cancel.load(std::memory_order_relaxed))
            return Result::cancelled;
        const auto start = data.getFramesAppended();
        const auto count = static_cast<int>(std::min<SamplePosition>(blockSize, readerLength - start));
        buffer.clear();
        if (!reader.read(&buffer, 0, count, start, true, true))
            return Result::readError;
        data.append({ buffer.getArrayOfReadPointers(), static_cast<std::size_t>(buffer.getNumChannels()) }, count);
        if (onProgress)
            onProgress();
    }
    data.complete();
    if (onProgress)
        onProgress();
    return Result::complete;
}

}

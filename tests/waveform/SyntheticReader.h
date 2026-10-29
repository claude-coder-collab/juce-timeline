#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <thread>
#include <utility>

struct SyntheticBehaviour
{
    std::int64_t failAt = -1;
    std::chrono::microseconds delayPerRead{ 0 };
    std::atomic<int>* readCount = nullptr;
};

class SyntheticReader final : public juce::AudioFormatReader
{
public:
    using Generator = std::function<float(int channel, std::int64_t position)>;

    SyntheticReader(std::int64_t length, int channels, Generator generatorIn, SyntheticBehaviour behaviourIn = {}) :
        AudioFormatReader(nullptr, "Synthetic"),
        generator(std::move(generatorIn)),
        behaviour(behaviourIn)
    {
        constexpr auto rate = 48000.0;
        sampleRate = rate;
        lengthInSamples = length;
        numChannels = static_cast<unsigned int>(channels);
        bitsPerSample = 32;
        usesFloatingPointData = true;
    }

    bool readSamples(int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer, juce::int64 startSampleInFile, int numSamples) override
    {
        if (behaviour.readCount != nullptr)
            ++*behaviour.readCount;
        if (behaviour.delayPerRead.count() > 0)
            std::this_thread::sleep_for(behaviour.delayPerRead);
        if (behaviour.failAt >= 0 && startSampleInFile + numSamples > behaviour.failAt)
            return false;
        for (auto channel = 0; channel < numDestChannels; ++channel)
        {
            if (destChannels[channel] == nullptr)
                continue;
            for (auto i = 0; i < numSamples; ++i)
            {
                const auto value = std::cmp_less(channel, numChannels) ? generator(channel, startSampleInFile + i) : 0.0f;
                std::memcpy(destChannels[channel] + startOffsetInDestBuffer + i, &value, sizeof(float));
            }
        }
        return true;
    }

private:
    Generator generator;
    SyntheticBehaviour behaviour;
};

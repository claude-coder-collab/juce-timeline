#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <thread>

struct SyntheticSettings
{
    double sampleRate = 48000.0;
    juce::int64 length = 0;
    int variant = 0;
    bool slow = false;
    juce::int64 failAt = -1;
};

class SyntheticReader final : public juce::AudioFormatReader
{
public:
    explicit SyntheticReader(const SyntheticSettings& settingsIn) :
        AudioFormatReader(nullptr, "Synthetic"),
        settings(settingsIn)
    {
        sampleRate = settings.sampleRate;
        lengthInSamples = settings.length;
        numChannels = 2;
        bitsPerSample = 32;
        usesFloatingPointData = true;
    }

    bool readSamples(int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer, juce::int64 startSampleInFile, int numSamples) override
    {
        if (settings.slow)
            std::this_thread::sleep_for(std::chrono::milliseconds{ 15 });
        if (settings.failAt >= 0 && startSampleInFile + numSamples > settings.failAt)
            return false;
        for (auto channel = 0; channel < numDestChannels; ++channel)
        {
            if (destChannels[channel] == nullptr)
                continue;
            for (auto i = 0; i < numSamples; ++i)
            {
                const auto value = generate(channel, startSampleInFile + i);
                std::memcpy(destChannels[channel] + startOffsetInDestBuffer + i, &value, sizeof(float));
            }
        }
        return true;
    }

private:
    [[nodiscard]] float generate(int channel, juce::int64 position) const
    {
        const auto variant = static_cast<double>(settings.variant);
        const auto seconds = static_cast<double>(position) / sampleRate;
        const auto period = 8.0 - std::fmod(variant * 1.7, 6.0);
        const auto bar = std::fmod(seconds, period);
        const auto decay = 1.5 + std::fmod(variant * 0.9, 3.0);
        const auto envelope = bar < period / 2.0 ? std::exp(-bar * decay) : 0.15 * (1.0 + std::sin(seconds * (0.7 + (variant * 0.3))));
        const auto slowSwell = 0.55 + (0.45 * std::sin(seconds * 2.0 * std::numbers::pi / (600.0 / (1.0 + variant))));
        const auto frequency = (channel == 0 ? 110.0 : 165.0) * (1.0 + (variant * 0.25));
        const auto tone = std::sin(2.0 * std::numbers::pi * frequency * seconds);
        const auto hash = static_cast<std::uint32_t>(position * 2654435761LL) ^ static_cast<std::uint32_t>((channel * 40503) + (settings.variant * 7919));
        const auto noise = (static_cast<double>(hash % 2001U) / 1000.0) - 1.0;
        const auto noiseShare = 0.2 + std::fmod(variant * 0.15, 0.5);
        return static_cast<float>(slowSwell * envelope * (((1.0 - noiseShare) * tone) + (noiseShare * noise)));
    }

    SyntheticSettings settings;
};

#include "jwaveform_PeakData.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <utility>

namespace jwaveform
{

namespace
{

std::int16_t toPeakValue(float value, bool roundUp)
{
    if (std::isnan(value))
        return 0;
    const auto scaled = static_cast<double>(std::clamp(value, -1.0f, 1.0f)) * 32767.0;
    return static_cast<std::int16_t>(roundUp ? std::ceil(scaled) : std::floor(scaled));
}

}

struct PeakData::Level
{
    std::vector<std::unique_ptr<PeakPair[]>> chunks; // NOLINT(*-avoid-c-arrays): fixed-size, uninitialised, never reallocated
    SamplePosition capacity = 0;
    SamplePosition written = 0;
    std::atomic<SamplePosition> valid{ 0 };
};

PeakData::PeakData(double sampleRateIn, SamplePosition length, int numChannelsIn, int samplesPerPeak) :
    sampleRate(sampleRateIn),
    numChannels(std::max(1, numChannelsIn)),
    levels(length, samplesPerPeak),
    blockMin(static_cast<std::size_t>(numChannels), std::numeric_limits<float>::max()),
    blockMax(static_cast<std::size_t>(numChannels), std::numeric_limits<float>::lowest())
{
    for (auto level = 0; level < levels.getLevelCount(); ++level)
        levelStorage.push_back(std::make_unique<Level>());
    for (auto level = 0; level < levels.getLevelCount(); ++level)
    {
        auto& storage = *levelStorage[static_cast<std::size_t>(level)];
        storage.capacity = levels.pairCountAt(level);
        storage.chunks.resize(static_cast<std::size_t>((storage.capacity + pairsPerChunk - 1) / pairsPerChunk));
    }
}

PeakData::~PeakData() = default;

SamplePosition PeakData::getValidPairs(int level) const
{
    assert(level >= 0 && level < levels.getLevelCount());
    return levelStorage[static_cast<std::size_t>(level)]->valid.load(std::memory_order_acquire);
}

bool PeakData::isComplete() const
{
    return completed.load(std::memory_order_acquire);
}

std::size_t PeakData::getMemoryBytes() const
{
    return memoryBytes.load(std::memory_order_relaxed);
}

PeakPair PeakData::getPair(int level, int channel, SamplePosition index) const
{
    assert(channel >= 0 && channel < numChannels);
    assert(index >= 0 && index < levelStorage[static_cast<std::size_t>(level)]->valid.load(std::memory_order_relaxed));
    return getPairUnpublished(level, channel, index);
}

void PeakData::writePair(int level, SamplePosition index, int channel, PeakPair pair)
{
    auto& storage = *levelStorage[static_cast<std::size_t>(level)];
    auto& chunk = storage.chunks[static_cast<std::size_t>(index / pairsPerChunk)];
    if (chunk == nullptr)
    {
        const auto count = static_cast<std::size_t>(pairsPerChunk) * static_cast<std::size_t>(numChannels);
        chunk = std::make_unique_for_overwrite<PeakPair[]>(count); // NOLINT(*-avoid-c-arrays)
        memoryBytes.fetch_add(count * sizeof(PeakPair), std::memory_order_relaxed);
    }
    chunk[static_cast<std::size_t>(((index % pairsPerChunk) * numChannels) + channel)] = pair;
}

void PeakData::emitLevel0Pair()
{
    auto& storage = *levelStorage[0];
    for (auto channel = 0; channel < numChannels; ++channel)
    {
        const auto c = static_cast<std::size_t>(channel);
        const auto anyValue = blockMin[c] <= blockMax[c];
        writePair(0, storage.written, channel, anyValue ? PeakPair{ .min = toPeakValue(blockMin[c], false), .max = toPeakValue(blockMax[c], true) } : PeakPair{});
        blockMin[c] = std::numeric_limits<float>::max();
        blockMax[c] = std::numeric_limits<float>::lowest();
    }
    ++storage.written;
    framesInBlock = 0;
}

void PeakData::append(std::span<const float* const> channels, int numFrames)
{
    assert(!isComplete());
    assert(std::cmp_greater_equal(channels.size(), numChannels));
    const auto spp = levels.getSamplesPerPeak();
    auto remaining = static_cast<int>(std::min<SamplePosition>(std::max(0, numFrames), getLength() - framesAppended));
    auto offset = 0;
    while (remaining > 0)
    {
        const auto count = std::min(remaining, spp - framesInBlock);
        for (auto channel = 0; channel < numChannels; ++channel)
        {
            const auto c = static_cast<std::size_t>(channel);
            const auto* begin = channels[c] + offset;
            const auto [lowest, highest] = std::minmax_element(begin, begin + count);
            blockMin[c] = std::min(blockMin[c], *lowest);
            blockMax[c] = std::max(blockMax[c], *highest);
        }
        framesInBlock += count;
        offset += count;
        remaining -= count;
        framesAppended += count;
        if (framesInBlock == spp)
            emitLevel0Pair();
    }
    propagate(false);
    publish();
}

void PeakData::complete()
{
    if (isComplete())
        return;
    if (framesInBlock > 0)
        emitLevel0Pair();
    propagate(true);
    publish();
    completed.store(true, std::memory_order_release);
}

void PeakData::propagate(bool final)
{
    for (auto level = 1; level < levels.getLevelCount(); ++level)
    {
        const auto& below = *levelStorage[static_cast<std::size_t>(level - 1)];
        auto& storage = *levelStorage[static_cast<std::size_t>(level)];
        const auto target = std::min(storage.capacity, final ? (below.written + 1) / 2 : below.written / 2);
        for (; storage.written < target; ++storage.written)
        {
            const auto first = storage.written * 2;
            const auto hasSecond = first + 1 < below.written;
            for (auto channel = 0; channel < numChannels; ++channel)
            {
                auto pair = getPairUnpublished(level - 1, channel, first);
                if (hasSecond)
                {
                    const auto second = getPairUnpublished(level - 1, channel, first + 1);
                    pair = { .min = std::min(pair.min, second.min), .max = std::max(pair.max, second.max) };
                }
                writePair(level, storage.written, channel, pair);
            }
        }
    }
}

PeakPair PeakData::getPairUnpublished(int level, int channel, SamplePosition index) const
{
    const auto& chunk = levelStorage[static_cast<std::size_t>(level)]->chunks[static_cast<std::size_t>(index / pairsPerChunk)];
    return chunk[static_cast<std::size_t>(((index % pairsPerChunk) * numChannels) + channel)];
}

void PeakData::publish()
{
    for (auto level = 0; level < levels.getLevelCount(); ++level)
    {
        auto& storage = *levelStorage[static_cast<std::size_t>(level)];
        storage.valid.store(storage.written, std::memory_order_release);
    }
}

}

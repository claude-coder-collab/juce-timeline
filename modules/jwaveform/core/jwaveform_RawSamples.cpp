#include "jwaveform_RawSamples.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace jwaveform
{

SampleBlock::SampleBlock(SamplePosition startIn, int numChannelsIn, int numFramesIn) :
    start(startIn),
    numChannels(std::max(1, numChannelsIn)),
    numFrames(std::max(0, numFramesIn)),
    samples(static_cast<std::size_t>(numChannels) * static_cast<std::size_t>(numFrames))
{
}

std::span<const float> SampleBlock::getChannel(int channel) const
{
    const auto frames = static_cast<std::size_t>(numFrames);
    return std::span{ samples }.subspan(static_cast<std::size_t>(std::clamp(channel, 0, numChannels - 1)) * frames, frames);
}

std::span<float> SampleBlock::getChannel(int channel)
{
    const auto frames = static_cast<std::size_t>(numFrames);
    return std::span{ samples }.subspan(static_cast<std::size_t>(std::clamp(channel, 0, numChannels - 1)) * frames, frames);
}

RawSamples::RawSamples(int numChannelsIn, std::vector<std::shared_ptr<const SampleBlock>> blocksIn) :
    numChannels(std::max(1, numChannelsIn)),
    blocks(std::move(blocksIn))
{
    std::erase(blocks, nullptr);
    std::ranges::sort(blocks, {}, [](const auto& block) { return block->getStart(); });
}

const SampleBlock* RawSamples::blockFor(SamplePosition position) const
{
    const auto after = std::ranges::upper_bound(blocks, position, {}, [](const auto& block) { return block->getStart(); });
    if (after == blocks.begin())
        return nullptr;
    const auto& block = *std::prev(after);
    return block->getRange().contains(position) ? block.get() : nullptr;
}

bool RawSamples::contains(SampleRange range) const
{
    auto position = range.start;
    while (position < range.end)
    {
        const auto* block = blockFor(position);
        if (block == nullptr)
            return false;
        position = block->getRange().end;
    }
    return true;
}

float RawSamples::get(int channel, SamplePosition position) const
{
    const auto* block = blockFor(position);
    if (block == nullptr || channel < 0 || channel >= block->getNumChannels())
        return 0.0f;
    return block->getChannel(channel)[static_cast<std::size_t>(position - block->getStart())];
}

bool usesRawSamples(const PeakData& data, double samplesPerPhysicalPixel)
{
    return samplesPerPhysicalPixel < static_cast<double>(data.getLevels().getSamplesPerPeak());
}

SampleRange rawRangeFor(double sourceStart, double sourceEnd)
{
    if (!std::isfinite(sourceStart) || !std::isfinite(sourceEnd) || !(sourceEnd > sourceStart))
        return {};
    return { .start = static_cast<SamplePosition>(std::floor(sourceStart)) - 1, .end = static_cast<SamplePosition>(std::ceil(sourceEnd)) + 1 };
}

void computeRawColumns(const RawSamples& samples, int channel, SamplePosition sourceLength, double sourceStart, double samplesPerColumn, std::span<ColumnPeak> columns)
{
    if (samples.isEmpty())
        return;
    const auto spc = std::isfinite(samplesPerColumn) && samplesPerColumn > 0.0 ? samplesPerColumn : 1.0;
    const auto firstChannel = channel == allChannels ? 0 : channel;
    const auto endChannel = channel == allChannels ? samples.getNumChannels() : channel + 1;
    const SampleRange source{ .start = 0, .end = sourceLength };

    for (std::size_t x = 0; x < columns.size(); ++x)
    {
        const auto from = sourceStart + (static_cast<double>(x) * spc);
        const auto start = static_cast<SamplePosition>(std::floor(from));
        const auto end = std::max(start + 1, static_cast<SamplePosition>(std::ceil(from + spc)));
        const auto range = SampleRange{ .start = start, .end = end };
        if (range.intersection(source) != range || !samples.contains(range))
            continue;
        const auto joined = SampleRange{ .start = range.start > 0 && samples.contains(range.start - 1) ? range.start - 1 : range.start, .end = range.end };

        auto low = std::numeric_limits<float>::max();
        auto high = std::numeric_limits<float>::lowest();
        for (auto c = firstChannel; c < endChannel; ++c)
        {
            for (auto position = joined.start; position < joined.end; ++position)
            {
                const auto value = samples.get(c, position);
                if (std::isnan(value))
                    continue;
                low = std::min(low, value);
                high = std::max(high, value);
            }
        }
        if (low > high)
            low = high = 0.0f;
        columns[x] = { .min = std::clamp(low, -1.0f, 1.0f), .max = std::clamp(high, -1.0f, 1.0f), .valid = true };
    }
}

}

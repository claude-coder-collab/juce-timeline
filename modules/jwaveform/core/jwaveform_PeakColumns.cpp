#include "jwaveform_PeakColumns.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace jwaveform
{

namespace
{

struct Accumulator
{
    std::int16_t min = std::numeric_limits<std::int16_t>::max();
    std::int16_t max = std::numeric_limits<std::int16_t>::min();
    bool any = false;

    void add(PeakPair pair)
    {
        min = std::min(min, pair.min);
        max = std::max(max, pair.max);
        any = true;
    }
};

void accumulatePairs(const PeakData& data, int channel, int level, SamplePosition first, SamplePosition last, Accumulator& result)
{
    for (auto index = first; index < last; ++index)
    {
        if (channel == allChannels)
        {
            for (auto c = 0; c < data.getNumChannels(); ++c)
                result.add(data.getPair(level, c, index));
        }
        else
        {
            result.add(data.getPair(level, channel, index));
        }
    }
}

ColumnPeak computeColumn(const PeakData& data, int channel, int startLevel, SampleRange range)
{
    const auto& levels = data.getLevels();
    Accumulator accumulator;
    for (auto level = startLevel; level >= 0 && !range.isEmpty(); --level)
    {
        const auto size = levels.samplesPerPairAt(level);
        const auto pairs = levels.pairsCovering(level, range);
        const auto valid = std::min(pairs.end, data.getValidPairs(level));
        if (valid > pairs.start)
            accumulatePairs(data, channel, level, pairs.start, valid, accumulator);
        range.start = std::max(range.start, valid * size);
    }
    if (!accumulator.any)
        return {};
    return { .min = PeakPair{ .min = accumulator.min }.minValue(), .max = PeakPair{ .max = accumulator.max }.maxValue(), .valid = true };
}

}

void computeColumns(const PeakData& data, int channel, double sourceStart, double samplesPerColumn, std::span<ColumnPeak> columns)
{
    const auto& levels = data.getLevels();
    const auto spc = std::isfinite(samplesPerColumn) && samplesPerColumn > 0.0 ? samplesPerColumn : 1.0;
    const auto level = levels.levelFor(spc);
    const SampleRange source{ .start = 0, .end = data.getLength() };
    for (std::size_t x = 0; x < columns.size(); ++x)
    {
        const auto from = sourceStart + (static_cast<double>(x) * spc);
        const auto start = static_cast<SamplePosition>(std::floor(from));
        const auto end = std::max(start + 1, static_cast<SamplePosition>(std::ceil(from + spc)));
        const auto range = SampleRange{ .start = start, .end = end }.intersection(source);
        columns[x] = range.isEmpty() ? ColumnPeak{} : computeColumn(data, channel, level, range);
    }
}

double WaveformEnvelope::gainAt(double sourcePosition) const
{
    auto gain = decibelsToGain(gainDb);
    if (!fadeIn.isEmpty())
        gain *= std::clamp((sourcePosition - static_cast<double>(fadeIn.start)) / static_cast<double>(fadeIn.length()), 0.0, 1.0);
    if (!fadeOut.isEmpty())
        gain *= std::clamp((static_cast<double>(fadeOut.end) - sourcePosition) / static_cast<double>(fadeOut.length()), 0.0, 1.0);
    return gain;
}

double decibelsToGain(double decibels)
{
    return std::pow(10.0, decibels / 20.0);
}

}

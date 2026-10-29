#include "jtimeline_RulerTicks.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace jtimeline::ruler
{

namespace
{

struct Candidate
{
    double seconds = 1.0;
    std::array<int, 3> subdivisions{};
};

constexpr std::size_t maxTicks = 100'000;

std::vector<Candidate> makeCandidates()
{
    std::vector<Candidate> result;
    for (auto exponent = -6; exponent < 0; ++exponent)
    {
        const auto decade = std::pow(10.0, exponent);
        result.push_back({ .seconds = decade, .subdivisions = { 10, 5, 2 } });
        result.push_back({ .seconds = 2.0 * decade, .subdivisions = { 4, 2, 0 } });
        result.push_back({ .seconds = 5.0 * decade, .subdivisions = { 5, 0, 0 } });
    }
    for (const auto unit : { 1.0, 60.0 })
    {
        result.push_back({ .seconds = unit, .subdivisions = { unit == 1.0 ? 10 : 6, unit == 1.0 ? 5 : 4, 2 } });
        result.push_back({ .seconds = 2.0 * unit, .subdivisions = { 4, 2, 0 } });
        result.push_back({ .seconds = 5.0 * unit, .subdivisions = { 5, 0, 0 } });
        result.push_back({ .seconds = 10.0 * unit, .subdivisions = { 10, 5, 2 } });
        result.push_back({ .seconds = 15.0 * unit, .subdivisions = { 3, 0, 0 } });
        result.push_back({ .seconds = 30.0 * unit, .subdivisions = { 6, 3, 2 } });
    }
    for (auto exponent = 0; exponent < 6; ++exponent)
    {
        const auto hours = std::pow(10.0, exponent);
        result.push_back({ .seconds = hours * 3600.0, .subdivisions = { exponent == 0 ? 6 : 10, exponent == 0 ? 4 : 5, 2 } });
        result.push_back({ .seconds = hours * 7200.0, .subdivisions = { 4, 2, 0 } });
        result.push_back({ .seconds = hours * 18000.0, .subdivisions = { 5, 0, 0 } });
    }
    return result;
}

const std::vector<Candidate>& candidates()
{
    static const auto list = makeCandidates();
    return list;
}

double positive(double value, double fallback)
{
    return std::isfinite(value) && value > 0.0 ? value : fallback;
}

}

RulerScale scaleFor(double samplesPerPixel, double sampleRate, const RulerDensity& density)
{
    const auto spp = positive(samplesPerPixel, 1.0);
    const auto rate = positive(sampleRate, 1.0);
    const auto minimumSeconds = std::max(density.minMajorPixels, 1.0) * spp / rate;

    const auto& list = candidates();
    const auto found = std::ranges::find_if(list, [minimumSeconds](const Candidate& candidate) { return candidate.seconds >= minimumSeconds; });
    auto chosen = found != list.end() ? *found : Candidate{ .seconds = list.back().seconds, .subdivisions = { 10, 5, 2 } };
    while (chosen.seconds < minimumSeconds)
        chosen.seconds *= 10.0;

    RulerScale scale{ .majorSeconds = chosen.seconds, .majorSamples = chosen.seconds * rate, .minorSamples = chosen.seconds * rate };
    for (const auto count : chosen.subdivisions)
    {
        if (count > 1 && scale.majorSamples / static_cast<double>(count) / spp >= density.minMinorPixels)
        {
            scale.minorSamples = scale.majorSamples / static_cast<double>(count);
            break;
        }
    }
    if (1.0 / spp >= density.minMinorPixels && scale.majorSamples > 1.0)
        scale.minorSamples = 1.0;
    return scale;
}

std::vector<RulerTick> ticksFor(const TimeMapping& mapping, double width, const RulerScale& scale)
{
    std::vector<RulerTick> ticks;
    if (!(width > 0.0) || !(scale.majorSamples > 0.0) || !(scale.minorSamples > 0.0))
        return ticks;
    const auto first = mapping.getStartPosition();
    const auto last = first + (width * mapping.getSamplesPerPixel());
    const auto inView = [first, last](double position) { return position >= first && position < last; };

    const auto addEvery = [&](double spacing, bool major) {
        const auto from = std::ceil(first / spacing);
        const auto to = std::floor(last / spacing);
        if (to - from >= static_cast<double>(maxTicks))
            return false;
        const auto count = static_cast<std::int64_t>(to - from);
        for (std::int64_t step = 0; step <= count; ++step)
        {
            const auto position = (from + static_cast<double>(step)) * spacing;
            if (!inView(position))
                continue;
            if (!major)
            {
                const auto ratio = position / scale.majorSamples;
                if (std::abs(ratio - std::round(ratio)) < 1.0e-6)
                    continue;
            }
            ticks.push_back({ .position = position, .major = major });
        }
        return true;
    };

    if (!addEvery(scale.majorSamples, true))
        return {};
    if (scale.minorSamples < scale.majorSamples)
        addEvery(scale.minorSamples, false);
    std::ranges::sort(ticks, {}, &RulerTick::position);
    return ticks;
}

juce::String formatTime(double seconds, double spacingSeconds)
{
    if (!std::isfinite(seconds))
        return {};
    const auto spacing = positive(spacingSeconds, 1.0);
    auto decimals = 0;
    if (std::abs(spacing - std::round(spacing)) > 1.0e-9 || spacing < 1.0)
        decimals = spacing >= 1.0e-3 ? 3 : std::clamp(static_cast<int>(std::ceil(-std::log10(spacing) - 1.0e-9)), 3, 6);

    std::int64_t unitsPerSecond = 1;
    for (auto digit = 0; digit < decimals; ++digit)
        unitsPerSecond *= 10;
    const auto units = std::llround(std::abs(seconds) * static_cast<double>(unitsPerSecond));
    const auto wholeSeconds = units / unitsPerSecond;
    const auto fraction = units % unitsPerSecond;
    const auto hours = wholeSeconds / 3600;
    const auto minutes = (wholeSeconds / 60) % 60;
    const auto secs = wholeSeconds % 60;

    juce::String text = units != 0 && seconds < 0.0 ? "-" : "";
    if (hours > 0)
        text << juce::String{ hours } << ":" << juce::String{ minutes }.paddedLeft('0', 2);
    else
        text << juce::String{ minutes };
    text << ":" << juce::String{ secs }.paddedLeft('0', 2);
    if (decimals > 0)
        text << "." << juce::String{ fraction }.paddedLeft('0', decimals);
    return text;
}

juce::String defaultLabel(double position, double sampleRate, double majorSpacingSamples)
{
    const auto rate = positive(sampleRate, 1.0);
    return formatTime(position / rate, majorSpacingSamples / rate);
}

}

#include <jwaveform/core/jwaveform_PeakData.h>

#include <catch2/catch_test_macros.hpp>

#include <array>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

using jwaveform::PeakData;
using jwaveform::PeakPair;

namespace
{

void appendMono(PeakData& data, const std::vector<float>& samples, std::size_t offset, std::size_t count)
{
    const std::array<const float*, 1> channels{ samples.data() + offset };
    data.append(channels, static_cast<int>(count));
}

std::int16_t scaled(float value, bool up)
{
    const auto v = static_cast<double>(value) * 32767.0;
    return static_cast<std::int16_t>(up ? std::ceil(v) : std::floor(v));
}

}

TEST_CASE("PeakData builds level 0 and publishes complete pairs only", "[peakdata]")
{
    std::vector<float> ramp(10);
    for (std::size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<float>(i) / 10.0f;

    PeakData data{ 48000.0, 10, 1, 4 };
    CHECK(data.getLevels().getLevelCount() == 3);
    appendMono(data, ramp, 0, 10);
    CHECK(data.getFramesAppended() == 10);
    CHECK(data.getValidPairs(0) == 2);
    CHECK(data.getValidPairs(1) == 1);
    CHECK(data.getValidPairs(2) == 0);
    CHECK_FALSE(data.isComplete());
    CHECK(data.getPair(0, 0, 0) == PeakPair{ .min = 0, .max = scaled(0.3f, true) });
    CHECK(data.getPair(0, 0, 1) == PeakPair{ .min = scaled(0.4f, false), .max = scaled(0.7f, true) });

    data.complete();
    CHECK(data.isComplete());
    CHECK(data.getValidPairs(0) == 3);
    CHECK(data.getValidPairs(1) == 2);
    CHECK(data.getValidPairs(2) == 1);
    CHECK(data.getPair(0, 0, 2) == PeakPair{ .min = scaled(0.8f, false), .max = scaled(0.9f, true) });
    CHECK(data.getPair(1, 0, 1) == data.getPair(0, 0, 2));
    CHECK(data.getPair(2, 0, 0) == PeakPair{ .min = 0, .max = scaled(0.9f, true) });
}

TEST_CASE("PeakData rounds outwards and clamps and ignores NaN", "[peakdata]")
{
    const std::vector<float> samples{ 0.5f, -0.5f, 2.0f, -3.0f, std::numeric_limits<float>::quiet_NaN() };
    PeakData data{ 48000.0, 5, 1, 2 };
    appendMono(data, samples, 0, samples.size());
    data.complete();
    CHECK(data.getPair(0, 0, 0) == PeakPair{ .min = -16384, .max = 16384 });
    CHECK(data.getPair(0, 0, 1) == PeakPair{ .min = -32767, .max = 32767 });
    CHECK(data.getPair(0, 0, 2) == PeakPair{ .min = 0, .max = 0 });
    CHECK(PeakPair{ .min = -32767, .max = 32767 }.minValue() == -1.0f);
    CHECK(PeakPair{ .min = -32767, .max = 32767 }.maxValue() == 1.0f);
}

TEST_CASE("PeakData gives the same result however frames are split", "[peakdata]")
{
    std::mt19937 random{ 7 };
    std::uniform_real_distribution<float> value{ -1.0f, 1.0f };
    constexpr std::size_t length = 50'000;
    std::vector<float> left(length);
    std::vector<float> right(length);
    std::ranges::generate(left, [&] { return value(random); });
    std::ranges::generate(right, [&] { return value(random); });

    PeakData whole{ 48000.0, length, 2 };
    const std::array<const float*, 2> all{ left.data(), right.data() };
    whole.append(all, static_cast<int>(length));
    whole.complete();

    PeakData pieces{ 48000.0, length, 2 };
    std::uniform_int_distribution<std::size_t> size{ 1, 3000 };
    for (std::size_t offset = 0; offset < length;)
    {
        const auto count = std::min(size(random), length - offset);
        const std::array<const float*, 2> part{ left.data() + offset, right.data() + offset };
        pieces.append(part, static_cast<int>(count));
        offset += count;
    }
    pieces.complete();

    for (auto level = 0; level < whole.getLevels().getLevelCount(); ++level)
    {
        REQUIRE(pieces.getValidPairs(level) == whole.getValidPairs(level));
        for (jwaveform::SamplePosition i = 0; i < whole.getValidPairs(level); ++i)
            for (auto channel = 0; channel < 2; ++channel)
                REQUIRE(pieces.getPair(level, channel, i) == whole.getPair(level, channel, i));
    }
}

TEST_CASE("PeakData higher levels summarise level 0", "[peakdata]")
{
    std::mt19937 random{ 3 };
    std::uniform_real_distribution<float> value{ -1.0f, 1.0f };
    constexpr std::size_t length = 40'000;
    std::vector<float> samples(length);
    std::ranges::generate(samples, [&] { return value(random); });
    PeakData data{ 48000.0, length, 1, 16 };
    appendMono(data, samples, 0, length);
    data.complete();

    for (auto level = 1; level < data.getLevels().getLevelCount(); ++level)
    {
        const auto span = data.getLevels().samplesPerPairAt(level) / data.getLevels().getSamplesPerPeak();
        for (jwaveform::SamplePosition i = 0; i < data.getValidPairs(level); ++i)
        {
            PeakPair expected{ .min = std::numeric_limits<std::int16_t>::max(), .max = std::numeric_limits<std::int16_t>::min() };
            for (auto j = i * span; j < std::min((i + 1) * span, data.getValidPairs(0)); ++j)
            {
                const auto pair = data.getPair(0, 0, j);
                expected = { .min = std::min(expected.min, pair.min), .max = std::max(expected.max, pair.max) };
            }
            REQUIRE(data.getPair(level, 0, i) == expected);
        }
    }
}

TEST_CASE("PeakData ignores frames past its length and finishes short sources", "[peakdata]")
{
    const std::vector<float> samples(100, 0.25f);
    PeakData data{ 48000.0, 64, 1, 16 };
    appendMono(data, samples, 0, samples.size());
    CHECK(data.getFramesAppended() == 64);
    CHECK(data.getValidPairs(0) == 4);

    PeakData shortSource{ 48000.0, 1000, 1, 16 };
    appendMono(shortSource, samples, 0, 20);
    shortSource.complete();
    CHECK(shortSource.isComplete());
    CHECK(shortSource.getValidPairs(0) == 2);
    CHECK(shortSource.getValidPairs(1) == 1);
}

TEST_CASE("PeakData allocates storage in chunks as it grows", "[peakdata]")
{
    PeakData data{ 48000.0, 48000LL * 3600, 2 };
    CHECK(data.getMemoryBytes() == 0);
    const std::vector<float> samples(64, 0.1f);
    const std::array<const float*, 2> channels{ samples.data(), samples.data() };
    data.append(channels, 64);
    const auto chunkBytes = static_cast<std::size_t>(PeakData::pairsPerChunk) * 2 * sizeof(PeakPair);
    CHECK(data.getMemoryBytes() == chunkBytes);
    CHECK(data.getNumChannels() == 2);
    CHECK(data.getSampleRate() == 48000.0);
}

TEST_CASE("PeakData handles an empty source", "[peakdata]")
{
    PeakData data{ 44100.0, 0, 0 };
    CHECK(data.getNumChannels() == 1);
    data.complete();
    CHECK(data.isComplete());
    CHECK(data.getValidPairs(0) == 0);
}

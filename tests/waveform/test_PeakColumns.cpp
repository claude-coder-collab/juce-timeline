#include <jwaveform/core/jwaveform_PeakColumns.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

#include <algorithm>
#include <random>
#include <vector>

using Catch::Approx;
using jwaveform::ColumnPeak;
using jwaveform::PeakData;
using jwaveform::SamplePosition;

namespace
{

std::vector<float> noise(std::size_t length, unsigned seed)
{
    std::mt19937 random{ seed };
    std::uniform_real_distribution<float> value{ -1.0f, 1.0f };
    std::vector<float> samples(length);
    std::ranges::generate(samples, [&] { return value(random); });
    return samples;
}

ColumnPeak bruteForce(const PeakData& data, int channel, SamplePosition start, SamplePosition end)
{
    const auto spp = data.getLevels().getSamplesPerPeak();
    std::int16_t low = 32767;
    std::int16_t high = -32767;
    const auto last = std::min((end + spp - 1) / spp, data.getValidPairs(0));
    for (auto i = start / spp; i < last; ++i)
    {
        const auto pair = data.getPair(0, channel, i);
        low = std::min(low, pair.min);
        high = std::max(high, pair.max);
    }
    return { .min = jwaveform::PeakPair{ .min = low }.minValue(), .max = jwaveform::PeakPair{ .max = high }.maxValue(), .valid = true };
}

}

TEST_CASE("Columns at level-0 resolution match the pairs", "[columns]")
{
    const auto samples = noise(1024, 1);
    PeakData data{ 48000.0, 1024, 1, 64 };
    const std::array<const float*, 1> channels{ samples.data() };
    data.append(channels, 1024);
    data.complete();

    std::vector<ColumnPeak> columns(16);
    jwaveform::computeColumns(data, 0, 0.0, 64.0, columns);
    for (std::size_t x = 0; x < columns.size(); ++x)
    {
        const auto pair = data.getPair(0, 0, static_cast<SamplePosition>(x));
        CHECK(columns[x] == ColumnPeak{ .min = pair.minValue(), .max = pair.maxValue(), .valid = true });
    }
}

TEST_CASE("Coarse columns read higher levels and match level 0", "[columns]")
{
    constexpr std::size_t length = 200'000;
    const auto samples = noise(length, 2);
    PeakData data{ 48000.0, length, 1, 64 };
    const std::array<const float*, 1> channels{ samples.data() };
    data.append(channels, static_cast<int>(length));
    data.complete();

    std::vector<ColumnPeak> columns(48);
    jwaveform::computeColumns(data, 0, 0.0, 4096.0, columns);
    for (std::size_t x = 0; x < columns.size(); ++x)
    {
        const auto start = static_cast<SamplePosition>(x) * 4096;
        CHECK(columns[x] == bruteForce(data, 0, start, std::min<SamplePosition>(start + 4096, length)));
    }
    CHECK(columns.back().valid);
}

TEST_CASE("Columns over a partial build fall back to finer levels", "[columns]")
{
    constexpr std::size_t length = 100'000;
    const auto samples = noise(length, 3);
    PeakData data{ 48000.0, length, 1, 64 };
    const std::array<const float*, 1> channels{ samples.data() };
    constexpr std::size_t appended = 50'000;
    data.append(channels, static_cast<int>(appended));

    const auto level = data.getLevels().levelFor(8192.0);
    REQUIRE(level > 0);
    REQUIRE(data.getValidPairs(level) * data.getLevels().samplesPerPairAt(level) < static_cast<SamplePosition>(appended) - 64);

    std::vector<ColumnPeak> columns(13);
    jwaveform::computeColumns(data, 0, 0.0, 8192.0, columns);
    const auto covered = data.getValidPairs(0) * 64;
    for (std::size_t x = 0; x < columns.size(); ++x)
    {
        const auto start = static_cast<SamplePosition>(x) * 8192;
        if (start >= covered)
            CHECK_FALSE(columns[x].valid);
        else
            CHECK(columns[x] == bruteForce(data, 0, start, std::min<SamplePosition>(start + 8192, covered)));
    }
}

TEST_CASE("Columns zoomed past level 0 stretch the pair they fall in", "[columns]")
{
    const auto samples = noise(256, 4);
    PeakData data{ 48000.0, 256, 1, 64 };
    const std::array<const float*, 1> channels{ samples.data() };
    data.append(channels, 256);
    data.complete();

    std::vector<ColumnPeak> columns(8);
    jwaveform::computeColumns(data, 0, 62.0, 0.5, columns);
    const auto first = data.getPair(0, 0, 0);
    const auto second = data.getPair(0, 0, 1);
    CHECK(columns[3] == ColumnPeak{ .min = first.minValue(), .max = first.maxValue(), .valid = true });
    CHECK(columns[4] == ColumnPeak{ .min = second.minValue(), .max = second.maxValue(), .valid = true });
}

TEST_CASE("Combined columns span every channel", "[columns]")
{
    const std::vector<float> left(64, 0.5f);
    const std::vector<float> right(64, -0.25f);
    PeakData data{ 48000.0, 64, 2, 64 };
    const std::array<const float*, 2> channels{ left.data(), right.data() };
    data.append(channels, 64);
    data.complete();

    std::vector<ColumnPeak> columns(1);
    jwaveform::computeColumns(data, jwaveform::allChannels, 0.0, 64.0, columns);
    CHECK(columns[0].max == Approx(0.5f).margin(1e-4));
    CHECK(columns[0].min == Approx(-0.25f).margin(1e-4));
    jwaveform::computeColumns(data, 1, 0.0, 64.0, columns);
    CHECK(columns[0].max == Approx(-0.25f).margin(1e-4));
}

TEST_CASE("Columns outside the source are invalid", "[columns]")
{
    const std::vector<float> samples(128, 0.5f);
    PeakData data{ 48000.0, 128, 1, 64 };
    const std::array<const float*, 1> channels{ samples.data() };
    data.append(channels, 128);
    data.complete();

    std::vector<ColumnPeak> columns(4);
    jwaveform::computeColumns(data, 0, -128.0, 64.0, columns);
    CHECK_FALSE(columns[0].valid);
    CHECK_FALSE(columns[1].valid);
    CHECK(columns[2].valid);
    CHECK(columns[3].valid);
    jwaveform::computeColumns(data, 0, 128.0, 64.0, columns);
    CHECK_FALSE(columns[0].valid);
    jwaveform::computeColumns(data, 0, 0.0, 0.0, columns);
    CHECK(columns[0].valid);
}

TEST_CASE("Envelope applies gain and linear fades", "[columns]")
{
    const jwaveform::WaveformEnvelope envelope{ .gainDb = -6.0, .fadeIn = { .start = 0, .end = 100 }, .fadeOut = { .start = 900, .end = 1000 } };
    const auto unity = jwaveform::decibelsToGain(-6.0);
    CHECK(unity == Approx(0.501).margin(1e-3));
    CHECK(envelope.gainAt(-10.0) == Approx(0.0));
    CHECK(envelope.gainAt(50.0) == Approx(unity * 0.5));
    CHECK(envelope.gainAt(500.0) == Approx(unity));
    CHECK(envelope.gainAt(975.0) == Approx(unity * 0.25));
    CHECK(envelope.gainAt(1000.0) == Approx(0.0));
    CHECK(jwaveform::WaveformEnvelope{}.gainAt(123.0) == Approx(1.0));
}

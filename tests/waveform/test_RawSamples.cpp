#include <jwaveform/core/jwaveform_RawSamples.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;
using jwaveform::ColumnPeak;
using jwaveform::RawSamples;
using jwaveform::SampleBlock;
using jwaveform::SamplePosition;

namespace
{

std::shared_ptr<const SampleBlock> rampBlock(SamplePosition start, int frames, int channels = 1)
{
    auto block = std::make_shared<SampleBlock>(start, channels, frames);
    for (auto channel = 0; channel < channels; ++channel)
    {
        const auto values = block->getChannel(channel);
        for (auto i = 0; i < frames; ++i)
            values[static_cast<std::size_t>(i)] = static_cast<float>(start + i) / 100.0f * (channel == 0 ? 1.0f : -1.0f);
    }
    return block;
}

}

TEST_CASE("RawSamples finds samples across blocks and reports gaps", "[samples]")
{
    const RawSamples none;
    CHECK(none.isEmpty());
    CHECK_FALSE(none.contains(SamplePosition{ 0 }));

    const RawSamples samples{ 1, { rampBlock(20, 10), nullptr, rampBlock(0, 10) } };
    CHECK_FALSE(samples.isEmpty());
    CHECK(samples.contains(jwaveform::SampleRange{ .start = 0, .end = 10 }));
    CHECK_FALSE(samples.contains(jwaveform::SampleRange{ .start = 5, .end = 25 }));
    CHECK(samples.contains(SamplePosition{ 29 }));
    CHECK_FALSE(samples.contains(SamplePosition{ 30 }));
    CHECK_FALSE(samples.contains(SamplePosition{ -1 }));
    CHECK(samples.get(0, 25) == Approx(0.25f));
    CHECK(samples.get(0, 15) == 0.0f);
    CHECK(samples.get(3, 25) == 0.0f);
}

TEST_CASE("rawRangeFor widens a fractional span by a sample either side", "[samples]")
{
    CHECK(jwaveform::rawRangeFor(10.5, 20.2) == jwaveform::SampleRange{ .start = 9, .end = 22 });
    CHECK(jwaveform::rawRangeFor(5.0, 5.0).isEmpty());
    CHECK(jwaveform::rawRangeFor(std::nan(""), 1.0).isEmpty());
}

TEST_CASE("Raw columns hold the exact range of their samples, joined to the previous one", "[samples]")
{
    const RawSamples samples{ 2, { rampBlock(0, 64, 2) } };
    std::vector<ColumnPeak> columns(4, ColumnPeak{ .min = -0.9f, .max = 0.9f, .valid = true });
    jwaveform::computeRawColumns(samples, 0, 64, 10.0, 4.0, columns);
    CHECK(columns[0].min == Approx(0.09f));
    CHECK(columns[0].max == Approx(0.13f));
    CHECK(columns[1].min == Approx(0.13f));
    CHECK(columns[1].max == Approx(0.17f));

    jwaveform::computeRawColumns(samples, jwaveform::allChannels, 64, 10.0, 4.0, columns);
    CHECK(columns[0].min == Approx(-0.13f));
    CHECK(columns[0].max == Approx(0.13f));
}

TEST_CASE("Raw columns leave columns without samples unchanged", "[samples]")
{
    const RawSamples samples{ 1, { rampBlock(0, 16) } };
    const ColumnPeak stretched{ .min = -0.5f, .max = 0.5f, .valid = true };
    std::vector<ColumnPeak> columns(4, stretched);
    jwaveform::computeRawColumns(samples, 0, 1000, 8.0, 4.0, columns);
    CHECK(columns[1] != stretched);
    CHECK(columns[2] == stretched);
    CHECK(columns[3] == stretched);

    std::vector<ColumnPeak> past(2, stretched);
    jwaveform::computeRawColumns(samples, 0, 12, 10.0, 2.0, past);
    CHECK(past[0] != stretched);
    CHECK(past[1] == stretched);

    jwaveform::computeRawColumns({}, 0, 12, 0.0, 1.0, past);
    CHECK(past[1] == stretched);
}

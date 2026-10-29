#include "SyntheticReader.h"

#include <jwaveform/core/jwaveform_PeakBuilder.h>

#include <catch2/catch_test_macros.hpp>

#include <array>

#include <cmath>
#include <vector>

using jwaveform::PeakBuilder;
using jwaveform::PeakData;

namespace
{

float testSignal(int channel, std::int64_t position)
{
    return std::sin(static_cast<float>(position) * 0.001f * static_cast<float>(channel + 1)) * 0.8f;
}

void requireEqual(const PeakData& a, const PeakData& b)
{
    for (auto level = 0; level < a.getLevels().getLevelCount(); ++level)
    {
        REQUIRE(a.getValidPairs(level) == b.getValidPairs(level));
        for (jwaveform::SamplePosition i = 0; i < a.getValidPairs(level); ++i)
            for (auto channel = 0; channel < a.getNumChannels(); ++channel)
                REQUIRE(a.getPair(level, channel, i) == b.getPair(level, channel, i));
    }
}

}

TEST_CASE("PeakBuilder builds from a reader", "[builder]")
{
    SyntheticReader reader{ 100'000, 2, testSignal };
    const auto data = PeakBuilder::createDataFor(reader);
    CHECK(data->getNumChannels() == 2);
    CHECK(data->getLength() == 100'000);
    CHECK(data->getSampleRate() == 48000.0);

    const std::atomic<bool> cancel{ false };
    auto progress = 0;
    CHECK(PeakBuilder{ reader, *data, 4096 }.run(cancel, [&] { ++progress; }) == PeakBuilder::Result::complete);
    CHECK(data->isComplete());
    CHECK(progress == 26);

    PeakData expected{ 48000.0, 100'000, 2 };
    std::vector<float> left(100'000);
    std::vector<float> right(100'000);
    for (std::int64_t i = 0; i < 100'000; ++i)
    {
        left[static_cast<std::size_t>(i)] = testSignal(0, i);
        right[static_cast<std::size_t>(i)] = testSignal(1, i);
    }
    const std::array<const float*, 2> channels{ left.data(), right.data() };
    expected.append(channels, 100'000);
    expected.complete();
    requireEqual(*data, expected);
}

TEST_CASE("PeakBuilder stops when cancelled and resumes where it stopped", "[builder]")
{
    SyntheticReader reader{ 100'000, 1, testSignal };
    const auto data = PeakBuilder::createDataFor(reader);
    std::atomic<bool> cancel{ false };
    CHECK(PeakBuilder{ reader, *data, 10'000 }.run(cancel, [&] { cancel = true; }) == PeakBuilder::Result::cancelled);
    CHECK(data->getFramesAppended() == 10'000);
    CHECK_FALSE(data->isComplete());

    std::atomic<int> reads{ 0 };
    SyntheticReader second{ 100'000, 1, testSignal, { .failAt = -1, .delayPerRead = {}, .readCount = &reads } };
    cancel = false;
    CHECK(PeakBuilder{ second, *data, 10'000 }.run(cancel) == PeakBuilder::Result::complete);
    CHECK(reads == 9);

    SyntheticReader fresh{ 100'000, 1, testSignal };
    const auto expected = PeakBuilder::createDataFor(fresh);
    CHECK(PeakBuilder{ fresh, *expected }.run(cancel) == PeakBuilder::Result::complete);
    requireEqual(*data, *expected);
}

TEST_CASE("PeakBuilder reports read errors and keeps what it read", "[builder]")
{
    SyntheticReader reader{ 100'000, 1, testSignal, { .failAt = 25'000, .delayPerRead = {}, .readCount = nullptr } };
    const auto data = PeakBuilder::createDataFor(reader);
    const std::atomic<bool> cancel{ false };
    CHECK(PeakBuilder{ reader, *data, 10'000 }.run(cancel) == PeakBuilder::Result::readError);
    CHECK(data->getFramesAppended() == 20'000);
    CHECK_FALSE(data->isComplete());
}

TEST_CASE("PeakBuilder completes an empty source at once", "[builder]")
{
    SyntheticReader reader{ 0, 1, testSignal };
    const auto data = PeakBuilder::createDataFor(reader);
    const std::atomic<bool> cancel{ false };
    CHECK(PeakBuilder{ reader, *data }.run(cancel) == PeakBuilder::Result::complete);
    CHECK(data->isComplete());
}

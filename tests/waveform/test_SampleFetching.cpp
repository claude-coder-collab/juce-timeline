#include "SyntheticReader.h"

#include <jwaveform/core/jwaveform_PeakCache.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>

using Catch::Approx;
using jwaveform::PeakCache;
using jwaveform::PeakState;
using jwaveform::SampleRange;
using jwaveform::WaveformSource;

namespace
{

constexpr std::size_t blockBytes = std::size_t{ 1000 } * 2 * sizeof(float);

bool runUntil(const std::function<bool()>& done, int timeoutMs = 10'000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (!done())
    {
        if (juce::Time::getMillisecondCounter() > end)
            return false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    }
    return true;
}

float valueAt(int channel, std::int64_t position)
{
    return std::sin(static_cast<float>(position) * 0.01f) * (channel == 0 ? 1.0f : 0.5f);
}

WaveformSource makeSource(const juce::String& key, std::atomic<int>& opens, std::int64_t failAt = -1)
{
    return { .key = key,
        .createReader = [&opens, failAt] -> std::unique_ptr<juce::AudioFormatReader> {
            ++opens;
            return std::make_unique<SyntheticReader>(100'000, 2, valueAt, SyntheticBehaviour{ .failAt = failAt });
        },
        .peaks = nullptr };
}

PeakCache::Options smallBlocks()
{
    PeakCache::Options options;
    options.sampleBlockFrames = 1000;
    return options;
}

}

TEST_CASE("PeakHandle fetches raw samples in blocks and notifies as they arrive", "[cache][samples]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    PeakCache cache{ smallBlocks() };
    std::atomic<int> opens{ 0 };
    auto notified = 0;
    const auto handle = cache.request(makeSource("a", opens), [&] { ++notified; });
    REQUIRE(runUntil([&] { return handle.getState() == PeakState::ready; }));

    const SampleRange range{ .start = 1500, .end = 3500 };
    CHECK(handle.getSamples(range).isEmpty());
    notified = 0;
    REQUIRE(runUntil([&] { return handle.getSamples(range).contains(range); }));
    CHECK(runUntil([&] { return notified > 0; }));

    const auto samples = handle.getSamples(range);
    CHECK(samples.getNumChannels() == 2);
    CHECK(samples.get(0, 2000) == Approx(valueAt(0, 2000)));
    CHECK(samples.get(1, 3499) == Approx(valueAt(1, 3499)));
    CHECK_FALSE(samples.contains(SampleRange{ .start = 0, .end = 1000 }));
    CHECK(cache.getSampleMemoryUsage() == 3 * blockBytes);
    CHECK(cache.getMemoryUsage() > cache.getSampleMemoryUsage());
    CHECK(opens == 2);

    CHECK(handle.getSamples({ .start = -50, .end = 0 }).isEmpty());
    REQUIRE(runUntil([&] { return handle.getSamples({ .start = 99'990, .end = 200'000 }).contains(SampleRange{ .start = 99'990, .end = 100'000 }); }));
}

TEST_CASE("Raw samples are evicted before peaks, least recently used first", "[cache][samples]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    PeakCache cache{ smallBlocks() };
    std::atomic<int> opens{ 0 };
    const auto handle = cache.request(makeSource("a", opens));
    REQUIRE(runUntil([&] { return handle.getState() == PeakState::ready; }));
    const SampleRange early{ .start = 0, .end = 1000 };
    const SampleRange late{ .start = 5000, .end = 6000 };
    REQUIRE(runUntil([&] { return handle.getSamples(early).contains(early); }));
    REQUIRE(runUntil([&] { return handle.getSamples(late).contains(late); }));

    const auto peaks = cache.getMemoryUsage() - cache.getSampleMemoryUsage();
    cache.setMemoryLimit(peaks + blockBytes);
    CHECK(cache.getSampleMemoryUsage() == blockBytes);
    CHECK(handle.getData() != nullptr);
    std::atomic<int> unused{ 0 };
    CHECK(cache.request(makeSource("a", unused)).getSamples(late).contains(late));
}

TEST_CASE("Raw samples are not fetched for ready peaks, missing handles or failed readers", "[cache][samples]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    CHECK(jwaveform::PeakHandle{}.getSamples({ .start = 0, .end = 10 }).isEmpty());

    PeakCache cache{ smallBlocks() };
    std::atomic<int> opens{ 0 };
    const auto ready = cache.request(makeSource("ready", opens));
    REQUIRE(runUntil([&] { return ready.getState() == PeakState::ready; }));
    const auto forPeaks = jwaveform::PeakHandle::forPeaks(ready.getData());
    CHECK(forPeaks.getSamples({ .start = 0, .end = 10 }).isEmpty());

    auto readerCount = 0;
    std::atomic<int> failingOpens{ 0 };
    const auto failing = cache.request({ .key = "failing",
        .createReader = [&failingOpens, &readerCount] -> std::unique_ptr<juce::AudioFormatReader> {
            ++failingOpens;
            if (readerCount++ > 0)
                return nullptr;
            return std::make_unique<SyntheticReader>(100'000, 1, valueAt);
        },
        .peaks = nullptr });
    REQUIRE(runUntil([&] { return failing.getState() == PeakState::ready; }));
    const SampleRange range{ .start = 0, .end = 10 };
    CHECK(failing.getSamples(range).isEmpty());
    REQUIRE(runUntil([&] { return !cache.isBuilding() && failingOpens == 2; }));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(failing.getSamples(range).isEmpty());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(failingOpens == 2);
}

TEST_CASE("Raw sample requests are bounded, newest first", "[cache][samples]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    auto options = smallBlocks();
    options.maxPendingSampleBlocks = 4;
    PeakCache cache{ options };
    std::atomic<int> opens{ 0 };
    const auto handle = cache.request(makeSource("a", opens));
    REQUIRE(runUntil([&] { return handle.getState() == PeakState::ready; }));
    const SampleRange wide{ .start = 0, .end = 50'000 };
    static_cast<void>(handle.getSamples(wide));
    const SampleRange recent{ .start = 90'000, .end = 91'000 };
    REQUIRE(runUntil([&] { return handle.getSamples(recent).contains(recent); }));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    CHECK(cache.getSampleMemoryUsage() <= 5 * blockBytes);
}

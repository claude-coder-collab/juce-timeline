#include "SyntheticReader.h"

#include <jwaveform/core/jwaveform_PeakCache.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>

using jwaveform::PeakCache;
using jwaveform::PeakState;
using jwaveform::WaveformSource;

namespace
{

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

struct SourceOptions
{
    std::int64_t length = 200'000;
    std::int64_t failAt = -1;
    std::chrono::microseconds delay{ 0 };
};

WaveformSource makeSource(const juce::String& key, std::atomic<int>& opens, SourceOptions options = {}, std::atomic<int>* reads = nullptr)
{
    return { .key = key,
        .createReader = [&opens, options, reads] -> std::unique_ptr<juce::AudioFormatReader> {
            ++opens;
            return std::make_unique<SyntheticReader>(options.length, 2, [](int, std::int64_t position) { return std::sin(static_cast<float>(position) * 0.01f); }, SyntheticBehaviour{ .failAt = options.failAt, .delayPerRead = options.delay, .readCount = reads });
        },
        .peaks = nullptr };
}

}

TEST_CASE("PeakCache builds each key once for many handles", "[cache]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    PeakCache cache;
    std::atomic<int> opens{ 0 };
    auto notified = 0;
    const auto first = cache.request(makeSource("a", opens), [&] { ++notified; });
    const auto second = cache.request(makeSource("a", opens));
    CHECK(first.getState() == PeakState::loading);
    REQUIRE(runUntil([&] { return first.getState() == PeakState::ready; }));
    CHECK(second.getState() == PeakState::ready);
    CHECK(opens == 1);
    CHECK(runUntil([&] { return notified > 0; }));
    CHECK(first.getData() == second.getData());
    CHECK(first.getData()->isComplete());
    CHECK(cache.getNumEntries() == 1);
    CHECK(cache.getMemoryUsage() > 0);
}

TEST_CASE("PeakCache reports sources that can't be opened or read", "[cache]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    PeakCache cache;
    const auto missing = cache.request({ .key = "missing", .createReader = [] { return std::unique_ptr<juce::AudioFormatReader>{}; }, .peaks = nullptr });
    const auto throwing = cache.request({ .key = "throwing", .createReader = [] -> std::unique_ptr<juce::AudioFormatReader> { throw std::runtime_error{ "no" }; }, .peaks = nullptr });
    std::atomic<int> opens{ 0 };
    const auto broken = cache.request(makeSource("broken", opens, { .length = 500'000, .failAt = 100'000 }));
    REQUIRE(runUntil([&] { return missing.getState() != PeakState::loading && throwing.getState() != PeakState::loading && broken.getState() != PeakState::loading; }));
    CHECK(missing.getState() == PeakState::cannotOpen);
    CHECK(missing.getData() == nullptr);
    CHECK(throwing.getState() == PeakState::cannotOpen);
    CHECK(broken.getState() == PeakState::readError);
    REQUIRE(broken.getData() != nullptr);
    CHECK(broken.getData()->getFramesAppended() == 65536);
}

TEST_CASE("PeakCache hands out ready-made peaks and empty handles", "[cache]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    PeakCache cache;
    auto peaks = std::make_shared<jwaveform::PeakData>(48000.0, 0, 1);
    peaks->complete();
    const auto ready = cache.request({ .key = {}, .createReader = {}, .peaks = peaks });
    CHECK(ready.getState() == PeakState::ready);
    CHECK(ready.getData() == peaks);
    CHECK(cache.getNumEntries() == 0);

    const auto empty = cache.request({});
    CHECK(empty.getState() == PeakState::none);
    CHECK(empty.getData() == nullptr);
    CHECK(jwaveform::PeakHandle{}.getState() == PeakState::none);
}

TEST_CASE("PeakCache cancels unrequested builds and resumes them later", "[cache]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    PeakCache cache;
    std::atomic<int> opens{ 0 };
    std::atomic<int> reads{ 0 };
    const SourceOptions slow{ .length = 65536LL * 40, .delay = std::chrono::milliseconds{ 10 } };
    {
        const auto handle = cache.request(makeSource("slow", opens, slow, &reads));
        REQUIRE(runUntil([&] { return reads >= 3; }));
    }
    REQUIRE(runUntil([&] { return !cache.isBuilding(); }));
    const auto readsWhenCancelled = reads.load();
    CHECK(readsWhenCancelled < 40);
    CHECK(cache.getNumEntries() == 1);

    const auto again = cache.request(makeSource("slow", opens, slow, &reads));
    REQUIRE(again.getData() != nullptr);
    CHECK_FALSE(again.getData()->isComplete());
    REQUIRE(runUntil([&] { return again.getState() == PeakState::ready; }, 20'000));
    CHECK(opens == 2);
    CHECK(reads == 40);
}

TEST_CASE("PeakCache evicts unused peaks beyond its memory bound", "[cache]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    PeakCache cache;
    std::atomic<int> opens{ 0 };
    {
        const auto a = cache.request(makeSource("a", opens));
        const auto b = cache.request(makeSource("b", opens));
        REQUIRE(runUntil([&] { return a.getState() == PeakState::ready && b.getState() == PeakState::ready; }));
    }
    const auto held = cache.request(makeSource("c", opens));
    REQUIRE(runUntil([&] { return held.getState() == PeakState::ready; }));
    CHECK(cache.getNumEntries() == 3);

    cache.setMemoryLimit(1);
    CHECK(cache.getNumEntries() == 1);
    CHECK(held.getData() != nullptr);

    const auto rebuilt = cache.request(makeSource("a", opens));
    REQUIRE(runUntil([&] { return rebuilt.getState() == PeakState::ready; }));
    CHECK(opens == 4);
}

TEST_CASE("PeakCache handles may outlive the cache", "[cache]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    std::atomic<int> opens{ 0 };
    jwaveform::PeakHandle handle;
    {
        PeakCache cache;
        handle = cache.request(makeSource("a", opens, { .length = 65536LL * 100, .delay = std::chrono::milliseconds{ 5 } }));
        runUntil([&] { return handle.getData() != nullptr; });
    }
    CHECK(handle.getData() != nullptr);
    handle = {};
}

TEST_CASE("PeakCache callbacks may release other handles", "[cache]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    PeakCache cache;
    std::atomic<int> opens{ 0 };
    jwaveform::PeakHandle second;
    auto secondCalls = 0;
    const auto first = cache.request(makeSource("a", opens), [&] { second = {}; });
    second = cache.request(makeSource("a", opens), [&] { ++secondCalls; });
    REQUIRE(runUntil([&] { return second.getState() == PeakState::none; }));
    CHECK(secondCalls == 0);
}

#include <jtimeline/core/jtimeline_RulerTicks.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using Catch::Approx;
using jtimeline::TimeMapping;
namespace ruler = jtimeline::ruler;

TEST_CASE("Ruler scale picks the closest round major spacing that fits a label", "[ruler]")
{
    const auto oneSecondPerPixel = ruler::scaleFor(48000.0, 48000.0);
    CHECK(oneSecondPerPixel.majorSeconds == Approx(120.0));
    CHECK(oneSecondPerPixel.majorSamples == Approx(120.0 * 48000.0));

    const auto zoomedIn = ruler::scaleFor(48.0, 48000.0);
    CHECK(zoomedIn.majorSeconds == Approx(0.1));
    CHECK(zoomedIn.minorSamples == Approx(480.0));

    const auto zoomedOut = ruler::scaleFor(48000.0 * 100.0, 48000.0);
    CHECK(zoomedOut.majorSeconds == Approx(5.0 * 3600.0));

    const auto extreme = ruler::scaleFor(1.0e12, 48000.0);
    CHECK(extreme.majorSamples / 1.0e12 >= 90.0);
}

TEST_CASE("Ruler minor ticks mark samples at sample-level zoom", "[ruler]")
{
    const auto scale = ruler::scaleFor(1.0 / 16.0, 48000.0);
    CHECK(scale.minorSamples == Approx(1.0));
    CHECK(scale.majorSeconds < 0.001);

    const auto coarse = ruler::scaleFor(1.0, 48000.0);
    CHECK(coarse.minorSamples > 1.0);
}

TEST_CASE("Ruler ticks cover the view once each, majors on multiples of the spacing", "[ruler]")
{
    const TimeMapping mapping{ 48.0, -4800 };
    const auto scale = ruler::scaleFor(48.0, 48000.0);
    const auto ticks = ruler::ticksFor(mapping, 1000.0, scale);
    REQUIRE_FALSE(ticks.empty());
    CHECK(ticks.front().position >= -4800.0);
    CHECK(ticks.back().position < -4800.0 + 48000.0);
    CHECK(std::ranges::is_sorted(ticks, {}, &jtimeline::RulerTick::position));
    CHECK(std::ranges::adjacent_find(ticks, {}, &jtimeline::RulerTick::position) == ticks.end());
    const auto majors = std::ranges::count_if(ticks, &jtimeline::RulerTick::major);
    CHECK(majors == 10);
    for (const auto& tick : ticks)
        if (tick.major)
            CHECK(std::fmod(std::abs(tick.position), scale.majorSamples) == Approx(0.0).margin(1.0e-6));
    CHECK(ruler::ticksFor(mapping, 0.0, scale).empty());
}

TEST_CASE("Ruler time format adapts to the spacing", "[ruler]")
{
    CHECK(ruler::formatTime(0.0, 1.0) == "0:00");
    CHECK(ruler::formatTime(75.0, 5.0) == "1:15");
    CHECK(ruler::formatTime(3725.0, 60.0) == "1:02:05");
    CHECK(ruler::formatTime(1.25, 0.25) == "0:01.250");
    CHECK(ruler::formatTime(1.0002, 0.0001) == "0:01.0002");
    CHECK(ruler::formatTime(0.000005, 0.000005) == "0:00.000005");
    CHECK(ruler::formatTime(-2.5, 0.5) == "-0:02.500");
    CHECK(ruler::formatTime(-0.0000001, 1.0) == "0:00");
    CHECK(ruler::formatTime(1.5, 1.5) == "0:01.500");
    CHECK(ruler::defaultLabel(48000.0 * 90.0, 48000.0, 48000.0 * 30.0) == "1:30");
}

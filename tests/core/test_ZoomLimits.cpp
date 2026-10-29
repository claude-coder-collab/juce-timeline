#include <jtimeline/core/jtimeline_TimeMapping.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using jtimeline::ZoomLimits;

TEST_CASE("ZoomLimits fit the whole timeline at the widest zoom", "[zoom]")
{
    const auto limits = ZoomLimits::forTimeline(48000.0, 48000LL * 60, 1000.0);
    CHECK(limits.minSamplesPerPixel == Approx(1.0 / 16.0));
    CHECK(limits.maxSamplesPerPixel == Approx(48000.0 * 60 / 1000.0));
}

TEST_CASE("ZoomLimits treat short timelines as ten seconds long", "[zoom]")
{
    const auto empty = ZoomLimits::forTimeline(48000.0, 0, 1000.0);
    CHECK(empty.maxSamplesPerPixel == Approx(480.0));
    const auto shortTimeline = ZoomLimits::forTimeline(44100.0, 100, 441.0);
    CHECK(shortTimeline.maxSamplesPerPixel == Approx(1000.0));
}

TEST_CASE("ZoomLimits accept a coarser deepest zoom", "[zoom]")
{
    const auto limits = ZoomLimits::forTimeline(48000.0, 48000, 1000.0, 32.0);
    CHECK(limits.minSamplesPerPixel == Approx(32.0));
}

TEST_CASE("ZoomLimits never invert", "[zoom]")
{
    const auto limits = ZoomLimits::forTimeline(1.0, 1, 1e9, 4.0);
    CHECK(limits.maxSamplesPerPixel == Approx(4.0));
    const auto zeroWidth = ZoomLimits::forTimeline(48000.0, 48000, 0.0);
    CHECK(zeroWidth.maxSamplesPerPixel == Approx(480000.0));
}

TEST_CASE("ZoomLimits clamp", "[zoom]")
{
    const ZoomLimits limits{ .minSamplesPerPixel = 0.5, .maxSamplesPerPixel = 100.0 };
    CHECK(limits.clamp(0.1) == Approx(0.5));
    CHECK(limits.clamp(50.0) == Approx(50.0));
    CHECK(limits.clamp(1000.0) == Approx(100.0));
    CHECK(limits.clamp(-3.0) == Approx(1.0));
}

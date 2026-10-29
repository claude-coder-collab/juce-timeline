#include <jtimeline/core/jtimeline_TimeMapping.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <limits>

using Catch::Approx;
using jtimeline::SamplePosition;
using jtimeline::SampleRange;
using jtimeline::TimeMapping;

TEST_CASE("TimeMapping maps samples to pixels relative to the view start", "[mapping]")
{
    const TimeMapping mapping{ 10.0, 1000 };
    CHECK(mapping.toPixel(1000) == Approx(0.0));
    CHECK(mapping.toPixel(1100) == Approx(10.0));
    CHECK(mapping.toPixel(900) == Approx(-10.0));
    CHECK(mapping.toPixelLength(55) == Approx(5.5));
    CHECK(mapping.getPixelsPerSample() == Approx(0.1));
}

TEST_CASE("TimeMapping finds the sample under a pixel", "[mapping]")
{
    const TimeMapping mapping{ 10.0, 1000 };
    CHECK(mapping.sampleAt(0.0) == 1000);
    CHECK(mapping.sampleAt(0.95) == 1009);
    CHECK(mapping.sampleAt(-0.05) == 999);
    CHECK(mapping.nearestSampleTo(0.04) == 1000);
    CHECK(mapping.nearestSampleTo(0.06) == 1001);
}

TEST_CASE("TimeMapping handles deep zoom with a fractional start", "[mapping]")
{
    const TimeMapping mapping{ 1.0 / 16.0, 50, 0.5 };
    CHECK(mapping.toPixel(50) == Approx(-8.0));
    CHECK(mapping.toPixel(51) == Approx(8.0));
    CHECK(mapping.sampleAt(0.0) == 50);
    CHECK(mapping.sampleAt(7.9) == 50);
    CHECK(mapping.sampleAt(8.0) == 51);
    CHECK(mapping.nearestSampleTo(0.0) == 51);
    CHECK(mapping.nearestSampleTo(-0.1) == 50);
}

TEST_CASE("TimeMapping normalises its start fraction", "[mapping]")
{
    const TimeMapping forward{ 1.0, 10, 2.25 };
    CHECK(forward.getStartSample() == 12);
    CHECK(forward.getStartFraction() == Approx(0.25));

    const TimeMapping backward{ 1.0, 10, -0.25 };
    CHECK(backward.getStartSample() == 9);
    CHECK(backward.getStartFraction() == Approx(0.75));

    const TimeMapping tiny{ 1.0, 10, -1e-20 };
    CHECK(tiny.getStartFraction() < 1.0);
    CHECK(tiny.getStartFraction() >= 0.0);
}

TEST_CASE("TimeMapping rejects invalid zoom", "[mapping]")
{
    CHECK(TimeMapping{ 0.0, 0 }.getSamplesPerPixel() == 1.0);
    CHECK(TimeMapping{ -2.0, 0 }.getSamplesPerPixel() == 1.0);
    CHECK(TimeMapping{ std::numeric_limits<double>::quiet_NaN(), 0 }.getSamplesPerPixel() == 1.0);
    CHECK(TimeMapping{ 1.0, 0 }.zoomedAround(std::numeric_limits<double>::infinity(), 0.0).getSamplesPerPixel() == 1.0);
}

TEST_CASE("TimeMapping keeps graphics coordinates near the view", "[mapping]")
{
    const TimeMapping mapping{ 1.0, 0 };
    CHECK(mapping.toGraphicsX(50, 100.0) == Approx(50.0f));
    CHECK(mapping.toGraphicsX(-1'000'000'000'000LL, 100.0) == Approx(-2.0f));
    CHECK(mapping.toGraphicsX(1'000'000'000'000LL, 100.0, 5.0) == Approx(105.0f));
}

TEST_CASE("TimeMapping stays precise far into a long timeline", "[mapping]")
{
    constexpr SamplePosition farAway = 192000LL * 86400 * 365;
    const TimeMapping mapping{ 1.0 / 16.0, farAway };
    CHECK(mapping.toPixel(farAway + 1) == Approx(16.0));
    CHECK(mapping.sampleAt(17.0) == farAway + 1);
    CHECK(mapping.toGraphicsX(farAway + 3, 1000.0) == Approx(48.0f));
}

TEST_CASE("TimeMapping visible range covers partly visible samples", "[mapping]")
{
    CHECK(TimeMapping{ 10.0, 1000 }.visibleRange(100.0) == SampleRange{ 1000, 2000 });
    CHECK(TimeMapping{ 10.0, 1000 }.visibleRange(100.5) == SampleRange{ 1000, 2005 });
    CHECK(TimeMapping{ 0.25, 0, 0.5 }.visibleRange(10.0) == SampleRange{ 0, 3 });
    CHECK(TimeMapping{ 1.0, 5 }.visibleRange(0.0).isEmpty());
}

TEST_CASE("TimeMapping zoom keeps the anchor position fixed", "[mapping]")
{
    const auto oldZoom = GENERATE(1.0 / 16.0, 0.3, 1.0, 64.0, 48000.0);
    const auto newZoom = GENERATE(1.0 / 16.0, 0.7, 2.0, 1000.0);
    const auto anchorPixel = GENERATE(0.0, 13.0, 517.25);
    const TimeMapping mapping{ oldZoom, 123'456'789, 0.375 };

    const auto zoomed = mapping.zoomedAround(newZoom, anchorPixel);
    CHECK(zoomed.getSamplesPerPixel() == newZoom);
    const auto before = static_cast<double>(mapping.getStartSample()) + mapping.getStartFraction() + (anchorPixel * oldZoom);
    const auto after = static_cast<double>(zoomed.getStartSample()) + zoomed.getStartFraction() + (anchorPixel * newZoom);
    CHECK(after == Approx(before).margin(1e-6));
    CHECK(zoomed.getStartFraction() >= 0.0);
    CHECK(zoomed.getStartFraction() < 1.0);
}

TEST_CASE("TimeMapping zoom round trip returns to the same view", "[mapping]")
{
    const TimeMapping mapping{ 1.0 / 16.0, 1'000'000, 0.0 };
    const auto back = mapping.zoomedAround(8.0, 300.0).zoomedAround(1.0 / 16.0, 300.0);
    CHECK(back.getStartSample() == mapping.getStartSample());
    CHECK(back.getStartFraction() == Approx(0.0).margin(1e-9));
}

TEST_CASE("TimeMapping scrolling", "[mapping]")
{
    const TimeMapping mapping{ 0.5, 100 };
    const auto scrolled = mapping.scrolledBy(5.0);
    CHECK(scrolled.getStartSample() == 102);
    CHECK(scrolled.getStartFraction() == Approx(0.5));
    CHECK(scrolled.scrolledBy(-5.0) == mapping);

    const auto placed = mapping.withSampleAt(5000, 40.0);
    CHECK(placed.toPixel(5000) == Approx(40.0));
    CHECK(mapping.withSampleAt(5000, 0.0).getStartSample() == 5000);
}

TEST_CASE("TimeMapping allows pre-roll before zero", "[mapping]")
{
    const auto mapping = TimeMapping{ 100.0, 0 }.scrolledBy(-3.0);
    CHECK(mapping.getStartSample() == -300);
    CHECK(mapping.toPixel(0) == Approx(3.0));
    CHECK(mapping.sampleAt(1.5) == -150);
}

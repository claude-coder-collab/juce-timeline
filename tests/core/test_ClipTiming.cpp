#include <jtimeline/core/jtimeline_TimeMapping.h>

#include <catch2/catch_test_macros.hpp>

using jtimeline::ClipTiming;
using jtimeline::SampleRange;

TEST_CASE("ClipTiming maps timeline positions to source positions", "[clip]")
{
    constexpr ClipTiming clip{ .start = 1000, .length = 500, .sourceOffset = 200 };
    STATIC_CHECK(clip.end() == 1500);
    STATIC_CHECK(clip.toSource(1000) == 200);
    STATIC_CHECK(clip.toSource(1499) == 699);
    STATIC_CHECK(clip.toTimeline(200) == 1000);
    STATIC_CHECK(clip.toTimeline(clip.toSource(1234)) == 1234);
    STATIC_CHECK(clip.timelineRange() == SampleRange{ 1000, 1500 });
    STATIC_CHECK(clip.sourceRange() == SampleRange{ 200, 700 });
}

TEST_CASE("ClipTiming finds the source region behind a timeline range", "[clip]")
{
    constexpr ClipTiming clip{ .start = 1000, .length = 500, .sourceOffset = 200 };
    CHECK(clip.sourceRangeFor({ 0, 5000 }) == SampleRange{ 200, 700 });
    CHECK(clip.sourceRangeFor({ 1100, 1200 }) == SampleRange{ 300, 400 });
    CHECK(clip.sourceRangeFor({ 900, 1010 }) == SampleRange{ 200, 210 });
    CHECK(clip.sourceRangeFor({ 1490, 2000 }) == SampleRange{ 690, 700 });
    CHECK(clip.sourceRangeFor({ 1500, 2000 }).isEmpty());
    CHECK(clip.sourceRangeFor({ 0, 1000 }).isEmpty());
}

TEST_CASE("ClipTiming works far from zero", "[clip]")
{
    constexpr auto day = 48000LL * 86400;
    constexpr ClipTiming clip{ .start = 30 * day, .length = 10, .sourceOffset = 5 * day };
    CHECK(clip.toSource((30 * day) + 3) == (5 * day) + 3);
}

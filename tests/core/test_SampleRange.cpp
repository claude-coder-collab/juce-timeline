#include <jwaveform/core/jwaveform_SampleRange.h>

#include <catch2/catch_test_macros.hpp>

using jwaveform::SampleRange;

TEST_CASE("SampleRange is half-open", "[range]")
{
    constexpr SampleRange range{ .start = 10, .end = 20 };
    CHECK(range.length() == 10);
    CHECK(range.contains(10));
    CHECK(range.contains(19));
    CHECK_FALSE(range.contains(20));
    CHECK_FALSE(range.contains(9));
}

TEST_CASE("SampleRange treats reversed ranges as empty", "[range]")
{
    constexpr SampleRange range{ .start = 20, .end = 10 };
    CHECK(range.isEmpty());
    CHECK(range.length() == 0);
    CHECK_FALSE(range.contains(15));
}

TEST_CASE("SampleRange intersection", "[range]")
{
    constexpr SampleRange range{ .start = 10, .end = 20 };
    CHECK(range.intersection({ 15, 30 }) == SampleRange{ 15, 20 });
    CHECK(range.intersection({ 0, 12 }) == SampleRange{ 10, 12 });
    CHECK(range.intersection({ 12, 14 }) == SampleRange{ 12, 14 });
    CHECK_FALSE(range.intersects({ 20, 30 }));
    CHECK_FALSE(range.intersects({ 0, 10 }));
    CHECK(range.intersection({ 20, 30 }).isEmpty());
    CHECK_FALSE(range.intersects({ 15, 15 }));
}

TEST_CASE("SampleRange handles negative positions", "[range]")
{
    constexpr SampleRange range{ .start = -100, .end = 50 };
    CHECK(range.length() == 150);
    CHECK(range.contains(-1));
    CHECK(range.intersection({ 0, 1000 }) == SampleRange{ 0, 50 });
}

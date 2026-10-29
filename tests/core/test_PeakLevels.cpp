#include <jwaveform/core/jwaveform_PeakLevels.h>

#include <catch2/catch_test_macros.hpp>

using jwaveform::PeakLevels;
using jwaveform::SampleRange;

TEST_CASE("PeakLevels counts pairs per level, including partial blocks", "[peaks]")
{
    const PeakLevels levels{ 1000, 64 };
    CHECK(levels.samplesPerPairAt(0) == 64);
    CHECK(levels.samplesPerPairAt(3) == 512);
    CHECK(levels.pairCountAt(0) == 16);
    CHECK(levels.pairCountAt(1) == 8);
    CHECK(levels.pairCountAt(4) == 1);
    CHECK(levels.getLevelCount() == 5);
}

TEST_CASE("PeakLevels stops at the first single-pair level", "[peaks]")
{
    CHECK(PeakLevels{ 64, 64 }.getLevelCount() == 1);
    CHECK(PeakLevels{ 65, 64 }.getLevelCount() == 2);
    CHECK(PeakLevels{ 128, 64 }.getLevelCount() == 2);
    CHECK(PeakLevels{ 129, 64 }.getLevelCount() == 3);
}

TEST_CASE("PeakLevels handles empty and degenerate sources", "[peaks]")
{
    const PeakLevels empty{ 0 };
    CHECK(empty.getLevelCount() == 1);
    CHECK(empty.pairCountAt(0) == 0);
    CHECK(empty.pairsCovering(0, { 0, 100 }).isEmpty());

    const PeakLevels negative{ -5, 0 };
    CHECK(negative.getSourceLength() == 0);
    CHECK(negative.getSamplesPerPeak() == 1);
}

TEST_CASE("PeakLevels sizes an hour of 48 kHz audio", "[peaks]")
{
    const PeakLevels levels{ 48000LL * 3600 };
    CHECK(levels.pairCountAt(0) == 2'700'000);
    CHECK(levels.pairCountAt(levels.getLevelCount() - 1) == 1);
    CHECK(levels.pairCountAt(levels.getLevelCount() - 2) > 1);
}

TEST_CASE("PeakLevels finds the pairs covering a source range", "[peaks]")
{
    const PeakLevels levels{ 1000, 64 };
    CHECK(levels.pairsCovering(0, { 0, 64 }) == SampleRange{ 0, 1 });
    CHECK(levels.pairsCovering(0, { 63, 65 }) == SampleRange{ 0, 2 });
    CHECK(levels.pairsCovering(1, { 100, 300 }) == SampleRange{ 0, 3 });
    CHECK(levels.pairsCovering(0, { -50, 2000 }) == SampleRange{ 0, 16 });
    CHECK(levels.pairsCovering(0, { 1000, 2000 }).isEmpty());
}

TEST_CASE("PeakLevels picks the coarsest level with at least one pair per pixel", "[peaks]")
{
    const PeakLevels levels{ 48000LL * 3600, 64 };
    CHECK(levels.levelFor(1.0) == 0);
    CHECK(levels.levelFor(64.0) == 0);
    CHECK(levels.levelFor(127.0) == 0);
    CHECK(levels.levelFor(128.0) == 1);
    CHECK(levels.levelFor(1000.0) == 3);
    CHECK(levels.levelFor(1e15) == levels.getLevelCount() - 1);
    CHECK(levels.levelFor(0.0) == 0);
    CHECK(levels.levelFor(-1.0) == 0);
}

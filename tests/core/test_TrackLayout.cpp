#include <jtimeline/core/jtimeline_LevelOfDetail.h>
#include <jtimeline/core/jtimeline_TimelineModel.h>
#include <jtimeline/core/jtimeline_TrackLayout.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using jtimeline::SampleRange;
using jtimeline::TrackLayout;

TEST_CASE("Track layout stacks heights and finds tracks by position", "[layout]")
{
    const std::vector heights{ 10, 20, 0, 30 };
    const TrackLayout layout{ heights };
    CHECK(layout.getNumTracks() == 4);
    CHECK(layout.getTotalHeight() == 60);
    CHECK(layout.getTop(1) == 10);
    CHECK(layout.getTop(4) == 60);
    CHECK(layout.getHeight(3) == 30);
    CHECK(layout.getHeight(9) == 0);
    CHECK(layout.indexAt(0) == 0);
    CHECK(layout.indexAt(29) == 1);
    CHECK(layout.indexAt(30) == 3);
    CHECK(layout.indexAt(60) == -1);
    CHECK(layout.indexAt(-1) == -1);

    CHECK(layout.visibleTracks(0, 10) == TrackLayout::Span{ .begin = 0, .end = 1 });
    CHECK(layout.visibleTracks(5, 10) == TrackLayout::Span{ .begin = 0, .end = 2 });
    CHECK(layout.visibleTracks(35, 100) == TrackLayout::Span{ .begin = 3, .end = 4 });
    CHECK(layout.visibleTracks(0, 0) == TrackLayout::Span{});
    CHECK(TrackLayout{}.visibleTracks(0, 100) == TrackLayout::Span{});
}

TEST_CASE("Track heights step through the list, snapping heights between steps", "[layout]")
{
    const auto& steps = jtimeline::trackHeights::defaultSteps;
    using jtimeline::trackHeights::stepped;
    CHECK(stepped(48, steps, 1) == 72);
    CHECK(stepped(48, steps, -1) == 32);
    CHECK(stepped(50, steps, 1) == 72);
    CHECK(stepped(50, steps, -1) == 48);
    CHECK(stepped(272, steps, 1) == 272);
    CHECK(stepped(24, steps, -1) == 24);
    CHECK(stepped(500, steps, 1) == 272);
    CHECK(stepped(10, steps, -1) == 24);
    CHECK(stepped(50, {}, 1) == 50);
}

TEST_CASE("Clip spans merge overlaps and small gaps", "[layout]")
{
    using jtimeline::mergeSpans;
    const std::vector<SampleRange> spans{ { .start = 50, .end = 60 }, { .start = 0, .end = 10 }, { .start = 5, .end = 20 }, { .start = 20, .end = 25 }, { .start = 30, .end = 30 }, { .start = 28, .end = 40 } };
    CHECK(mergeSpans(spans, 1) == std::vector<SampleRange>{ { .start = 0, .end = 25 }, { .start = 28, .end = 40 }, { .start = 50, .end = 60 } });
    CHECK(mergeSpans(spans, 4) == std::vector<SampleRange>{ { .start = 0, .end = 40 }, { .start = 50, .end = 60 } });
    CHECK(mergeSpans(spans, 100) == std::vector<SampleRange>{ { .start = 0, .end = 60 } });
    CHECK(mergeSpans({}, 1).empty());
}

TEST_CASE("Level of detail follows on-screen width", "[layout]")
{
    using jtimeline::ClipTier;
    const jtimeline::DetailThresholds thresholds;
    CHECK(jtimeline::tierFor(40.0, thresholds) == ClipTier::full);
    CHECK(jtimeline::tierFor(39.9, thresholds) == ClipTier::reduced);
    CHECK(jtimeline::tierFor(4.0, thresholds) == ClipTier::reduced);
    CHECK(jtimeline::tierFor(1.0, thresholds) == ClipTier::minimal);
    CHECK(jtimeline::tierFor(0.99, thresholds) == ClipTier::coverage);
}

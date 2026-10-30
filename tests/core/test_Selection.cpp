#include <jtimeline/core/jtimeline_Selection.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using jtimeline::ClipId;
using jtimeline::SelectMode;
using jtimeline::TrackId;

TEST_CASE("Selecting replaces, adds or toggles", "[selection]")
{
    jtimeline::Selection selection;
    CHECK(selection.isEmpty());
    CHECK(selection.select(ClipId{ 1 }, SelectMode::replace));
    CHECK_FALSE(selection.select(ClipId{ 1 }, SelectMode::replace));
    CHECK(selection.select(ClipId{ 2 }, SelectMode::add));
    CHECK_FALSE(selection.select(ClipId{ 2 }, SelectMode::add));
    CHECK(selection.getClips().size() == 2);
    CHECK(selection.select(ClipId{ 1 }, SelectMode::toggle));
    CHECK_FALSE(selection.contains(ClipId{ 1 }));
    CHECK(selection.select(ClipId{ 1 }, SelectMode::toggle));
    CHECK(selection.contains(ClipId{ 1 }));

    CHECK(selection.select(TrackId{ 5 }, SelectMode::add));
    CHECK(selection.getClips().size() == 2);
    CHECK(selection.select(TrackId{ 6 }, SelectMode::replace));
    CHECK(selection.getClips().empty());
    CHECK(selection.getTracks().size() == 1);
    CHECK(selection.select(ClipId{ 3 }, SelectMode::replace));
    CHECK(selection.getTracks().empty());
}

TEST_CASE("Selection sets, retains and clears", "[selection]")
{
    jtimeline::Selection selection;
    const std::vector clips{ ClipId{ 1 }, ClipId{ 2 }, ClipId{ 3 } };
    CHECK(selection.setClips(clips));
    CHECK_FALSE(selection.setClips(clips));
    CHECK_FALSE(selection.addClips(std::vector{ ClipId{ 2 } }));
    CHECK(selection.addClips(std::vector{ ClipId{ 4 } }));
    CHECK(selection.retainClips([](ClipId id) { return id != ClipId{ 2 }; }));
    CHECK_FALSE(selection.retainClips([](ClipId) { return true; }));
    CHECK(selection.getClips().size() == 3);

    CHECK(selection.setTracks(std::vector{ TrackId{ 1 }, TrackId{ 2 } }));
    CHECK(selection.retainTracks([](TrackId id) { return id == TrackId{ 1 }; }));
    CHECK(selection.getTracks().size() == 1);

    CHECK(selection.setTimeRange(jtimeline::SampleRange{ .start = 10, .end = 20 }));
    CHECK_FALSE(selection.setTimeRange(jtimeline::SampleRange{ .start = 10, .end = 20 }));
    CHECK(selection.clearObjects());
    CHECK_FALSE(selection.clearObjects());
    CHECK(selection.getTimeRange().has_value());
    CHECK(selection.setTimeRange(jtimeline::SampleRange{ .start = 30, .end = 30 }));
    CHECK_FALSE(selection.getTimeRange().has_value());
    CHECK(selection.isEmpty());
}

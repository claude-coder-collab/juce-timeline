#include <jtimeline/core/jtimeline_KeyboardNavigation.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <vector>

using jtimeline::ClipId;
using jtimeline::CursorMove;
using jtimeline::Direction;
using jtimeline::KeyboardCursor;
using jtimeline::TrackId;
namespace navigation = jtimeline::navigation;

namespace
{

struct Layout
{
    std::vector<TrackId> tracks{ TrackId{ 1 }, TrackId{ 2 }, TrackId{ 3 }, TrackId{ 4 } };
    std::map<ClipId, jtimeline::ClipInfo> clips;
    int queries = 0;

    ClipId add(TrackId track, jtimeline::SamplePosition start, jtimeline::SamplePosition length)
    {
        const auto id = ClipId{ clips.size() + 1 };
        jtimeline::ClipInfo info;
        info.track = track;
        info.start = start;
        info.length = length;
        clips[id] = info;
        return id;
    }

    jtimeline::NavigationContext context(jtimeline::SampleRange visible, bool withAdjacent = false)
    {
        jtimeline::NavigationContext result{
            .tracks = tracks,
            .firstVisibleTrack = 0,
            .visible = visible,
            .length = 1'000'000,
            .clipsInRange = [this](TrackId track, jtimeline::SamplePosition start, jtimeline::SamplePosition end) {
                ++queries;
                std::vector<ClipId> ids;
                for (const auto& [id, info] : clips)
                    if (info.track == track && info.start < end && info.start + info.length > start)
                        ids.push_back(id);
                return ids; },
            .clipInfo = [this](ClipId id) -> std::optional<jtimeline::ClipInfo> {
                const auto found = clips.find(id);
                return found != clips.end() ? std::optional{ found->second } : std::nullopt;
            },
            .adjacentClip = {},
        };
        if (withAdjacent)
            result.adjacentClip = [](TrackId, ClipId, Direction) { return std::optional{ ClipId{ 99 } }; };
        return result;
    }
};

}

TEST_CASE("With no cursor, arrows go to the first visible clip of the first visible track", "[navigation]")
{
    Layout layout;
    layout.add(TrackId{ 2 }, 100, 50);
    layout.add(TrackId{ 2 }, 2000, 50);
    const auto visibleFirst = layout.add(TrackId{ 2 }, 1500, 50);
    auto context = layout.context({ .start = 1000, .end = 5000 });
    context.firstVisibleTrack = 1;
    CHECK(navigation::move({}, CursorMove::down, context) == KeyboardCursor{ .track = TrackId{ 2 }, .clip = visibleFirst });
    CHECK(navigation::move({}, CursorMove::escape, context).isEmpty());
    context.firstVisibleTrack = 0;
    CHECK(navigation::move({}, CursorMove::right, context) == KeyboardCursor{ .track = TrackId{ 1 }, .clip = std::nullopt });
}

TEST_CASE("Left and right follow start order with ties by draw order, staying at the ends", "[navigation]")
{
    Layout layout;
    const auto a = layout.add(TrackId{ 1 }, 0, 100);
    const auto b = layout.add(TrackId{ 1 }, 500, 100);
    const auto c = layout.add(TrackId{ 1 }, 500, 50);
    const auto far = layout.add(TrackId{ 1 }, 900'000, 100);
    layout.add(TrackId{ 2 }, 200, 100);
    const auto context = layout.context({ .start = 0, .end = 1000 });

    const KeyboardCursor onA{ .track = TrackId{ 1 }, .clip = a };
    CHECK(navigation::move(onA, CursorMove::right, context).clip == b);
    CHECK(navigation::move({ .track = TrackId{ 1 }, .clip = b }, CursorMove::right, context).clip == c);
    CHECK(navigation::move({ .track = TrackId{ 1 }, .clip = c }, CursorMove::left, context).clip == b);
    CHECK(navigation::move({ .track = TrackId{ 1 }, .clip = b }, CursorMove::left, context).clip == a);
    CHECK(navigation::move(onA, CursorMove::left, context) == onA);

    layout.queries = 0;
    CHECK(navigation::move({ .track = TrackId{ 1 }, .clip = c }, CursorMove::right, context).clip == far);
    CHECK(layout.queries > 1);
    CHECK(navigation::move({ .track = TrackId{ 1 }, .clip = far }, CursorMove::left, context).clip == c);
    CHECK(navigation::move({ .track = TrackId{ 1 }, .clip = far }, CursorMove::right, context).clip == far);

    CHECK(navigation::move(onA, CursorMove::right, layout.context({ .start = 0, .end = 1000 }, true)).clip == ClipId{ 99 });
}

TEST_CASE("Up and down keep the time position, falling back to the nearest visible clip and then the track", "[navigation]")
{
    Layout layout;
    const auto top = layout.add(TrackId{ 1 }, 1000, 500);
    const auto under = layout.add(TrackId{ 2 }, 800, 400);
    const auto over = layout.add(TrackId{ 2 }, 900, 400);
    const auto near = layout.add(TrackId{ 3 }, 3000, 100);
    layout.add(TrackId{ 3 }, 4500, 100);
    layout.add(TrackId{ 3 }, 90'000, 100);
    const auto context = layout.context({ .start = 0, .end = 10'000 });

    const auto second = navigation::move({ .track = TrackId{ 1 }, .clip = top }, CursorMove::down, context);
    CHECK(second == KeyboardCursor{ .track = TrackId{ 2 }, .clip = over });
    const auto third = navigation::move({ .track = TrackId{ 2 }, .clip = under }, CursorMove::down, context);
    CHECK(third == KeyboardCursor{ .track = TrackId{ 3 }, .clip = near });
    const auto fourth = navigation::move(third, CursorMove::down, context);
    CHECK(fourth == KeyboardCursor{ .track = TrackId{ 4 }, .clip = std::nullopt });
    CHECK(navigation::move(fourth, CursorMove::down, context) == fourth);
    CHECK(navigation::move({ .track = TrackId{ 1 }, .clip = top }, CursorMove::up, context).clip == top);
}

TEST_CASE("Escape leaves a clip for its track, and right or enter go back in", "[navigation]")
{
    Layout layout;
    const auto clip = layout.add(TrackId{ 2 }, 100, 100);
    const auto context = layout.context({ .start = 0, .end = 1000 });
    const auto onTrack = navigation::move({ .track = TrackId{ 2 }, .clip = clip }, CursorMove::escape, context);
    CHECK(onTrack == KeyboardCursor{ .track = TrackId{ 2 }, .clip = std::nullopt });
    CHECK(navigation::move(onTrack, CursorMove::escape, context) == onTrack);
    CHECK(navigation::move(onTrack, CursorMove::left, context) == onTrack);
    CHECK(navigation::move(onTrack, CursorMove::enter, context).clip == clip);
    CHECK(navigation::move(onTrack, CursorMove::right, context).clip == clip);
    CHECK(navigation::move(onTrack, CursorMove::up, context) == KeyboardCursor{ .track = TrackId{ 1 }, .clip = std::nullopt });
    const KeyboardCursor emptyTrack{ .track = TrackId{ 1 }, .clip = std::nullopt };
    CHECK(navigation::move(emptyTrack, CursorMove::enter, context) == emptyTrack);

    layout.clips.erase(clip);
    CHECK(navigation::move({ .track = TrackId{ 2 }, .clip = clip }, CursorMove::down, context) == KeyboardCursor{ .track = TrackId{ 3 }, .clip = std::nullopt });
    CHECK(navigation::move({ .track = TrackId{ 9 }, .clip = std::nullopt }, CursorMove::down, context) == KeyboardCursor{ .track = TrackId{ 1 }, .clip = std::nullopt });
}

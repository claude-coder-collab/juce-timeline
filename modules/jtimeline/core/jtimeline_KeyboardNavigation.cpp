#include "jtimeline_KeyboardNavigation.h"

#include <algorithm>
#include <tuple>
#include <utility>

namespace jtimeline::navigation
{

namespace
{

constexpr SamplePosition minimumWindow = 1024;

struct Candidate
{
    ClipId id{};
    SamplePosition start = 0;
    std::size_t order = 0;

    [[nodiscard]] std::pair<SamplePosition, std::size_t> key() const { return { start, order }; }
};

std::vector<Candidate> candidatesIn(TrackId track, SampleRange range, const NavigationContext& context)
{
    std::vector<Candidate> result;
    const auto clamped = range.intersection({ .start = 0, .end = context.length });
    if (clamped.isEmpty() || !context.clipsInRange || !context.clipInfo)
        return result;
    const auto ids = context.clipsInRange(track, clamped.start, clamped.end);
    for (std::size_t order = 0; order < ids.size(); ++order)
        if (const auto info = context.clipInfo(ids[order]))
            result.push_back({ .id = ids[order], .start = info->start, .order = order });
    return result;
}

std::optional<ClipId> clipUnder(TrackId track, SamplePosition position, const NavigationContext& context)
{
    const auto candidates = candidatesIn(track, { .start = position, .end = position + 1 }, context);
    return candidates.empty() ? std::nullopt : std::optional{ candidates.back().id };
}

std::optional<ClipId> nearestVisible(TrackId track, SamplePosition position, const NavigationContext& context)
{
    const auto candidates = candidatesIn(track, context.visible, context);
    const auto best = std::ranges::min_element(candidates, {}, [position](const Candidate& candidate) {
        const auto distance = candidate.start > position ? candidate.start - position : position - candidate.start;
        return std::tuple{ distance, candidate.start, candidate.order };
    });
    return best != candidates.end() ? std::optional{ best->id } : std::nullopt;
}

int indexOf(const std::vector<TrackId>& tracks, TrackId track)
{
    const auto found = std::ranges::find(tracks, track);
    return found != tracks.end() ? static_cast<int>(found - tracks.begin()) : -1;
}

}

std::optional<ClipId> firstClipIn(TrackId track, SampleRange range, const NavigationContext& context)
{
    const auto candidates = candidatesIn(track, range, context);
    const auto best = std::ranges::min_element(candidates, {}, &Candidate::key);
    return best != candidates.end() ? std::optional{ best->id } : std::nullopt;
}

std::optional<ClipId> adjacentClip(TrackId track, ClipId clip, Direction direction, const NavigationContext& context)
{
    if (context.adjacentClip)
        return context.adjacentClip(track, clip, direction);
    const auto info = context.clipInfo ? context.clipInfo(clip) : std::nullopt;
    if (!info)
        return std::nullopt;

    const auto start = info->start;
    auto window = std::max(context.visible.length(), minimumWindow);
    while (true)
    {
        const auto next = direction == Direction::next;
        const SampleRange range = next ? SampleRange{ .start = start, .end = std::min(start + window, context.length) } : SampleRange{ .start = std::max<SamplePosition>(start - window, 0), .end = start + 1 };
        const auto candidates = candidatesIn(track, range, context);
        const auto self = std::ranges::find(candidates, clip, &Candidate::id);
        const auto selfOrder = self != candidates.end() ? self->order : std::size_t{ 0 };

        std::optional<Candidate> best;
        for (const auto& candidate : candidates)
        {
            if (candidate.id == clip)
                continue;
            const auto after = candidate.start > start || (candidate.start == start && candidate.order > selfOrder);
            const auto before = candidate.start < start || (candidate.start == start && candidate.order < selfOrder);
            if (next && after && (!best || candidate.key() < best->key()))
                best = candidate;
            if (!next && before && candidate.start >= range.start && (!best || candidate.key() > best->key()))
                best = candidate;
        }
        if (best)
            return best->id;
        if (next ? range.end >= context.length : range.start <= 0)
            return std::nullopt;
        window *= 2;
    }
}

KeyboardCursor move(const KeyboardCursor& cursor, CursorMove direction, const NavigationContext& context)
{
    auto current = cursor;
    std::optional<ClipInfo> info;
    if (current.clip)
    {
        info = context.clipInfo ? context.clipInfo(*current.clip) : std::nullopt;
        if (info)
            current.track = info->track;
        else
            current.clip.reset();
    }
    const auto index = current.track ? indexOf(context.tracks, *current.track) : -1;
    if (index < 0)
        current = {};

    if (!current.track)
    {
        if (direction == CursorMove::escape || context.tracks.empty())
            return current;
        const auto first = context.tracks[static_cast<std::size_t>(std::clamp(context.firstVisibleTrack, 0, static_cast<int>(context.tracks.size()) - 1))];
        return { .track = first, .clip = firstClipIn(first, context.visible, context) };
    }

    const auto track = *current.track;
    const auto neighbour = [&](int step) -> std::optional<TrackId> {
        const auto target = index + step;
        if (target < 0 || std::cmp_greater_equal(target, context.tracks.size()))
            return std::nullopt;
        return context.tracks[static_cast<std::size_t>(target)];
    };

    if (current.clip && info)
    {
        switch (direction)
        {
            case CursorMove::left:
            case CursorMove::right:
            {
                const auto found = adjacentClip(track, *current.clip, direction == CursorMove::left ? Direction::previous : Direction::next, context);
                return found ? KeyboardCursor{ .track = track, .clip = found } : current;
            }
            case CursorMove::up:
            case CursorMove::down:
            {
                const auto target = neighbour(direction == CursorMove::up ? -1 : 1);
                if (!target)
                    return current;
                auto clip = clipUnder(*target, info->start, context);
                if (!clip)
                    clip = nearestVisible(*target, info->start, context);
                return { .track = target, .clip = clip };
            }
            case CursorMove::escape:
                return { .track = track, .clip = std::nullopt };
            case CursorMove::enter:
                return current;
        }
        return current;
    }

    switch (direction)
    {
        case CursorMove::up:
        case CursorMove::down:
        {
            const auto target = neighbour(direction == CursorMove::up ? -1 : 1);
            return target ? KeyboardCursor{ .track = target, .clip = std::nullopt } : current;
        }
        case CursorMove::right:
        case CursorMove::enter:
        {
            const auto clip = firstClipIn(track, context.visible, context);
            return clip ? KeyboardCursor{ .track = track, .clip = clip } : current;
        }
        case CursorMove::left:
        case CursorMove::escape:
            return current;
    }
    return current;
}

}

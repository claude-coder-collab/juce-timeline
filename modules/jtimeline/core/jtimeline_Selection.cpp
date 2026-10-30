#include "jtimeline_Selection.h"

#include <algorithm>

namespace jtimeline
{

namespace
{

template <typename Id>
bool selectIn(std::unordered_set<Id>& ids, Id id, SelectMode mode)
{
    switch (mode)
    {
        case SelectMode::replace:
        {
            if (ids.size() == 1 && ids.contains(id))
                return false;
            ids.clear();
            ids.insert(id);
            return true;
        }
        case SelectMode::add:
            return ids.insert(id).second;
        case SelectMode::toggle:
            if (ids.erase(id) == 0)
                ids.insert(id);
            return true;
    }
    return false;
}

template <typename Id>
bool replaceWith(std::unordered_set<Id>& ids, std::span<const Id> next)
{
    std::unordered_set<Id> replacement{ next.begin(), next.end() };
    if (replacement == ids)
        return false;
    ids = std::move(replacement);
    return true;
}

}

bool Selection::select(ClipId clip, SelectMode mode)
{
    auto changed = false;
    if (mode == SelectMode::replace && !tracks.empty())
    {
        tracks.clear();
        changed = true;
    }
    return selectIn(clips, clip, mode) || changed;
}

bool Selection::select(TrackId track, SelectMode mode)
{
    auto changed = false;
    if (mode == SelectMode::replace && !clips.empty())
    {
        clips.clear();
        changed = true;
    }
    return selectIn(tracks, track, mode) || changed;
}

bool Selection::setClips(std::span<const ClipId> ids)
{
    return replaceWith(clips, ids);
}

bool Selection::addClips(std::span<const ClipId> ids)
{
    const auto before = clips.size();
    clips.insert(ids.begin(), ids.end());
    return clips.size() != before;
}

bool Selection::setTracks(std::span<const TrackId> ids)
{
    return replaceWith(tracks, ids);
}

bool Selection::setTimeRange(std::optional<SampleRange> range)
{
    if (range && range->isEmpty())
        range.reset();
    if (range == timeRange)
        return false;
    timeRange = range;
    return true;
}

bool Selection::clearObjects()
{
    if (clips.empty() && tracks.empty())
        return false;
    clips.clear();
    tracks.clear();
    return true;
}

bool Selection::retainClips(const std::function<bool(ClipId)>& keep)
{
    return std::erase_if(clips, [&keep](ClipId id) { return !keep(id); }) > 0;
}

bool Selection::retainTracks(const std::function<bool(TrackId)>& keep)
{
    return std::erase_if(tracks, [&keep](TrackId id) { return !keep(id); }) > 0;
}

}

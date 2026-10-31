#pragma once

#include "jtimeline_Ids.h"
#include "jtimeline_TimeMapping.h"

#include <functional>
#include <optional>
#include <span>
#include <unordered_set>

namespace jtimeline
{

/** How a click or keyboard move changes the selection. */
enum class SelectMode {
    replace, /**< Select only the item. */
    add, /**< Add the item to the selection. */
    toggle /**< Add the item, or remove it if it is selected. */
};

//==============================================================================
/**
    A view's selection: clips and tracks by id, and an optional time range. Clip and track
    selection are object based; the time range is separate.
*/
class Selection
{
public:
    /** Returns the selected clips, in no particular order. */
    [[nodiscard]] const std::unordered_set<ClipId>& getClips() const { return clips; }
    /** Returns the selected tracks, in no particular order. */
    [[nodiscard]] const std::unordered_set<TrackId>& getTracks() const { return tracks; }
    /** Returns the selected time range, if any; never empty. */
    [[nodiscard]] const std::optional<SampleRange>& getTimeRange() const { return timeRange; }

    /** Returns true if a clip is selected. */
    [[nodiscard]] bool contains(ClipId clip) const { return clips.contains(clip); }
    /** Returns true if a track is selected. */
    [[nodiscard]] bool contains(TrackId track) const { return tracks.contains(track); }
    /** Returns true if nothing is selected. */
    [[nodiscard]] bool isEmpty() const { return clips.empty() && tracks.empty() && !timeRange; }

    /** Selects a clip; replace also deselects every track. Returns true if anything changed. */
    bool select(ClipId clip, SelectMode mode);
    /** Selects a track; replace also deselects every clip. Returns true if anything changed. */
    bool select(TrackId track, SelectMode mode);
    /** Replaces the selected clips. Returns true if anything changed. */
    bool setClips(std::span<const ClipId> ids);
    /** Adds clips. Returns true if anything changed. */
    bool addClips(std::span<const ClipId> ids);
    /** Replaces the selected tracks. Returns true if anything changed. */
    bool setTracks(std::span<const TrackId> ids);
    /** Sets the time range; an empty range clears it. Returns true if anything changed. */
    bool setTimeRange(std::optional<SampleRange> range);
    /** Deselects every clip and track, keeping the time range. Returns true if anything changed. */
    bool clearObjects();

    /** Deselects the clips a predicate rejects. Returns true if anything changed. */
    bool retainClips(const std::function<bool(ClipId)>& keep);
    /** Deselects the tracks a predicate rejects. Returns true if anything changed. */
    bool retainTracks(const std::function<bool(TrackId)>& keep);

    /** Compares two selections. */
    friend bool operator==(const Selection&, const Selection&) = default;

private:
    std::unordered_set<ClipId> clips;
    std::unordered_set<TrackId> tracks;
    std::optional<SampleRange> timeRange;
};

}

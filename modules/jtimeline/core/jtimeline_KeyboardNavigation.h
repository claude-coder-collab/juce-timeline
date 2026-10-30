#pragma once

#include "jtimeline_TimelineModel.h"

#include <functional>
#include <optional>
#include <vector>

namespace jtimeline
{

/** Where the keyboard cursor is: on a clip (and its track), on a track, or nowhere. */
struct KeyboardCursor
{
    std::optional<TrackId> track; /**< The cursor's track; set whenever clip is. */
    std::optional<ClipId> clip; /**< The cursor's clip, if it is on one. */

    /** Returns true if the cursor is on nothing. */
    [[nodiscard]] bool isEmpty() const { return !track && !clip; }

    /** Compares two cursors. */
    friend bool operator==(const KeyboardCursor&, const KeyboardCursor&) = default;
};

/** A keyboard cursor move. */
enum class CursorMove {
    left, /**< The previous clip on the track. */
    right, /**< The next clip on the track, or from a track to its first visible clip. */
    up, /**< The track above. */
    down, /**< The track below. */
    enter, /**< From a track to its first visible clip. */
    escape /**< From a clip to its track. */
};

/** What keyboard navigation reads: the view's tracks and visible area, and the model. */
struct NavigationContext
{
    std::vector<TrackId> tracks; /**< The tracks in display order. */
    int firstVisibleTrack = 0; /**< The index of the topmost visible track. */
    SampleRange visible; /**< The visible time range. */
    SamplePosition length = 0; /**< The timeline length. */
    std::function<std::vector<ClipId>(TrackId, SamplePosition, SamplePosition)> clipsInRange; /**< As TimelineModel::clipsInRange. */
    std::function<std::optional<ClipInfo>(ClipId)> clipInfo; /**< As TimelineModel::clipInfo. */
    std::function<std::optional<ClipId>(TrackId, ClipId, Direction)> adjacentClip; /**< As TimelineModel::adjacentClip; optional. */
};

/** Keyboard navigation between clips and tracks. */
namespace navigation
{

/**
    Returns where a move takes the cursor. With no cursor, any move but escape goes to the first
    visible clip of the first visible track, or to that track if it has no visible clip. On a clip,
    left and right go to the previous and next clip by start (ties by draw order) and stay at either
    end; up and down go to the adjacent track's clip under the current clip's start, else its
    visible clip nearest by start, else the track itself; escape goes to the clip's track. On a
    track, up and down go to the adjacent track, and right and enter to its first visible clip.
    A cursor on a clip that no longer exists counts as on its track.
*/
[[nodiscard]] KeyboardCursor move(const KeyboardCursor& cursor, CursorMove direction, const NavigationContext& context);

/**
    Returns the previous or next clip on a track by start, ties by draw order, using
    adjacentClip if set and otherwise clipsInRange over windows that double until one holds an
    answer or covers the timeline.
*/
[[nodiscard]] std::optional<ClipId> adjacentClip(TrackId track, ClipId clip, Direction direction, const NavigationContext& context);

/** Returns a track's earliest-starting clip within a range (ties by draw order), if any. */
[[nodiscard]] std::optional<ClipId> firstClipIn(TrackId track, SampleRange range, const NavigationContext& context);

}

}

#pragma once

#include "jtimeline_Ids.h"

#include <variant>
#include <vector>

namespace jtimeline
{

/** Tracks were added, removed or reordered, or a track's info changed. */
struct TracksChanged
{
    /** Compares two changes. */
    friend bool operator==(const TracksChanged&, const TracksChanged&) = default;
};

/** Clips were added, removed, moved or edited. */
struct ClipsChanged
{
    std::vector<ClipId> ids; /**< Every clip affected, including removed ones. */

    /** Compares two changes. */
    friend bool operator==(const ClipsChanged&, const ClipsChanged&) = default;
};

/** Markers or the loop range changed. */
struct MarkersChanged
{
    /** Compares two changes. */
    friend bool operator==(const MarkersChanged&, const MarkersChanged&) = default;
};

/** Anything may have changed, including the sample rate and length; every view re-reads the model. */
struct FullReset
{
    /** Compares two changes. */
    friend bool operator==(const FullReset&, const FullReset&) = default;
};

/** A change a model reports through TimelineNotifier::changed(). */
using Change = std::variant<TracksChanged, ClipsChanged, MarkersChanged, FullReset>;

}

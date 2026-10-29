#pragma once

#include <array>
#include <span>
#include <vector>

namespace jtimeline
{

/** Track height steps for keyboard height changes. */
namespace trackHeights
{

/** The default height steps, in pixels. */
inline constexpr std::array defaultSteps{ 24, 32, 48, 72, 112, 176, 272 };

/**
    Returns the next step above (direction > 0) or below (direction < 0) a height. A height between
    steps moves to the nearest step in that direction; a height past the last step in that
    direction moves to that step, so heights stay within the steps. Empty steps leave it unchanged.
*/
[[nodiscard]] int stepped(int height, std::span<const int> steps, int direction);

}

//==============================================================================
/** The vertical positions of a list of tracks, stacked from 0 with no gaps. */
class TrackLayout
{
public:
    /** Creates a layout with no tracks. */
    TrackLayout() = default;
    /** Creates a layout from track heights in display order; negative heights count as 0. */
    explicit TrackLayout(std::span<const int> heights);

    /** Returns the number of tracks. */
    [[nodiscard]] int getNumTracks() const { return static_cast<int>(tops.size()) - 1; }
    /** Returns the top of a track, or the total height for an index past the end. */
    [[nodiscard]] int getTop(int index) const;
    /** Returns a track's height, or 0 for an index out of range. */
    [[nodiscard]] int getHeight(int index) const;
    /** Returns the height of all tracks. */
    [[nodiscard]] int getTotalHeight() const { return tops.back(); }
    /** Returns the track containing a y position, or -1 if none does. */
    [[nodiscard]] int indexAt(int y) const;

    /** A range of track indices, [begin, end). */
    struct Span
    {
        int begin = 0; /**< The first index. */
        int end = 0; /**< One past the last index. */

        /** Compares two spans. */
        friend bool operator==(const Span&, const Span&) = default;
    };

    /** Returns the tracks at least partly inside [top, top + height). */
    [[nodiscard]] Span visibleTracks(int top, int height) const;

private:
    std::vector<int> tops{ 0 };
};

}

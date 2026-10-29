#include "jtimeline_TrackLayout.h"

#include <algorithm>
#include <iterator>

namespace jtimeline
{

int trackHeights::stepped(int height, std::span<const int> steps, int direction)
{
    if (steps.empty() || direction == 0)
        return height;
    if (direction > 0)
    {
        const auto found = std::ranges::upper_bound(steps, height);
        return found != steps.end() ? *found : steps.back();
    }
    const auto found = std::ranges::lower_bound(steps, height);
    return found != steps.begin() ? *std::prev(found) : steps.front();
}

TrackLayout::TrackLayout(std::span<const int> heights)
{
    tops.reserve(heights.size() + 1);
    for (const auto height : heights)
        tops.push_back(tops.back() + std::max(height, 0));
}

int TrackLayout::getTop(int index) const
{
    return tops[static_cast<std::size_t>(std::clamp(index, 0, getNumTracks()))];
}

int TrackLayout::getHeight(int index) const
{
    if (index < 0 || index >= getNumTracks())
        return 0;
    return getTop(index + 1) - getTop(index);
}

int TrackLayout::indexAt(int y) const
{
    if (y < 0 || y >= getTotalHeight())
        return -1;
    const auto found = std::ranges::upper_bound(tops, y);
    return static_cast<int>(std::distance(tops.begin(), found)) - 1;
}

TrackLayout::Span TrackLayout::visibleTracks(int top, int height) const
{
    if (height <= 0 || getNumTracks() == 0)
        return {};
    const auto bottom = top + height;
    const auto begin = static_cast<int>(std::distance(tops.begin(), std::ranges::upper_bound(tops, top))) - 1;
    const auto end = static_cast<int>(std::distance(tops.begin(), std::ranges::lower_bound(tops, bottom)));
    return { .begin = std::clamp(begin, 0, getNumTracks()), .end = std::clamp(end, 0, getNumTracks()) };
}

}

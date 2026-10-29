#pragma once

#include <algorithm>
#include <cstdint>

namespace jwaveform
{

/** A position in samples. Signed so that positions before the start of a timeline can be represented. */
using SamplePosition = std::int64_t;

//==============================================================================
/** A half-open range of sample positions: [start, end). */
struct SampleRange
{
    SamplePosition start = 0; /**< The first sample in the range. */
    SamplePosition end = 0; /**< One past the last sample in the range. */

    /** Returns true if the range has no samples. */
    [[nodiscard]] constexpr bool isEmpty() const { return end <= start; }
    /** Returns the number of samples in the range, or 0 if it is empty. */
    [[nodiscard]] constexpr SamplePosition length() const { return isEmpty() ? 0 : end - start; }
    /** Returns true if the position is in the range. */
    [[nodiscard]] constexpr bool contains(SamplePosition position) const { return position >= start && position < end; }
    /** Returns true if the two ranges share at least one sample. */
    [[nodiscard]] constexpr bool intersects(const SampleRange& other) const { return start < other.end && other.start < end && !isEmpty() && !other.isEmpty(); }
    /** Returns the samples in both ranges; empty (at this range's start) if they do not intersect. */
    [[nodiscard]] constexpr SampleRange intersection(const SampleRange& other) const
    {
        if (!intersects(other))
            return { .start = start, .end = start };
        return { .start = std::max(start, other.start), .end = std::min(end, other.end) };
    }

    /** Compares two ranges. */
    friend constexpr bool operator==(const SampleRange&, const SampleRange&) = default;
};

}

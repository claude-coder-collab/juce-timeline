#pragma once

#include "jwaveform_PeakData.h"

#include <span>

namespace jwaveform
{

/** How a multi-channel source is drawn. */
enum class ChannelMode {
    stacked, /**< One lane per channel. */
    combined /**< One lane holding the lowest minimum and highest maximum across channels. */
};

/** The value range drawn in one pixel column. */
struct ColumnPeak
{
    float min = 0.0f; /**< The lowest value, in [-1, 1]. */
    float max = 0.0f; /**< The highest value, in [-1, 1]. */
    bool valid = false; /**< False when no data covers the column yet, or it lies outside the source. */

    /** Compares two columns. */
    friend bool operator==(const ColumnPeak&, const ColumnPeak&) = default;
};

/** Selects every channel, combined, in computeColumns(). */
inline constexpr int allChannels = -1;

/**
    Fills one ColumnPeak per column for a span of the source starting at sourceStart, each
    column covering samplesPerColumn source samples.

    Reads from the coarsest level with at least one pair per column. Where that level is not yet
    built, the column is completed from the coarsest finer level that covers it, at worst level 0.
    Columns with no data are marked invalid. When zoomed in past level 0, each column shows the
    level-0 pair it falls in, so the waveform is drawn stretched. Cost is proportional to the
    number of columns, not samples.

    @param channel  a channel index, or allChannels to combine them
*/
void computeColumns(const PeakData& data, int channel, double sourceStart, double samplesPerColumn, std::span<ColumnPeak> columns);

//==============================================================================
/** Gain applied to a waveform as drawn: clip gain and linear fades. Display only. */
struct WaveformEnvelope
{
    double gainDb = 0.0; /**< Clip gain in decibels; 0 is unity. */
    SampleRange fadeIn{}; /**< Source positions over which the gain ramps from 0 to 1; empty for none. */
    SampleRange fadeOut{}; /**< Source positions over which the gain ramps from 1 to 0; empty for none. */

    /** Returns the linear gain at a source position. */
    [[nodiscard]] double gainAt(double sourcePosition) const;

    /** Compares two envelopes. */
    friend bool operator==(const WaveformEnvelope&, const WaveformEnvelope&) = default;
};

/** Converts decibels to a linear gain factor. */
[[nodiscard]] double decibelsToGain(double decibels);

}

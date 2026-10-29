#pragma once

#include "jtimeline_TimeMapping.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace jtimeline
{

/** A tick on the time ruler. */
struct RulerTick
{
    double position = 0.0; /**< The tick's timeline position in samples; ticks need not fall on whole samples. */
    bool major = false; /**< Major ticks are taller and labelled. */

    /** Compares two ticks. */
    friend bool operator==(const RulerTick&, const RulerTick&) = default;
};

/** The tick spacing chosen for a zoom level. */
struct RulerScale
{
    double majorSeconds = 1.0; /**< Seconds between major ticks. */
    double majorSamples = 1.0; /**< Samples between major ticks. */
    double minorSamples = 1.0; /**< Samples between minor ticks; equal to majorSamples when there are none. */
};

/** How dense ruler ticks may get. */
struct RulerDensity
{
    double minMajorPixels = 90.0; /**< The least space between major ticks, which must fit a label. */
    double minMinorPixels = 6.0; /**< The least space between minor ticks. */
};

/**
    Returns a major tick's label: the tick position in samples, the timeline sample rate and the
    samples between major ticks, so the label can show only the precision the spacing needs.
*/
using RulerFormatter = std::function<juce::String(double position, double sampleRate, double majorSpacingSamples)>;

/** Ruler tick spacing and labels. */
namespace ruler
{

/**
    Returns the tick spacing for a zoom level. Major ticks fall on round times (1, 2 or 5 times a
    power of ten below a second; 1, 2, 5, 10, 15 and 30 seconds and minutes; then hours), as close
    as density allows. Minor ticks subdivide them, except at sample-level zoom, where they mark
    individual samples.
*/
[[nodiscard]] RulerScale scaleFor(double samplesPerPixel, double sampleRate, const RulerDensity& density = {});

/** Returns the ticks visible in a view of the given width, in order, major ticks included once. */
[[nodiscard]] std::vector<RulerTick> ticksFor(const TimeMapping& mapping, double width, const RulerScale& scale);

/**
    Formats seconds as h:mm:ss.mmm, leaving out zero hours, leaving out milliseconds when the
    spacing is a whole number of seconds, and adding digits down to microseconds when the spacing
    is finer than a millisecond. Negative values start with "-".
*/
[[nodiscard]] juce::String formatTime(double seconds, double spacingSeconds);

/** The default RulerFormatter, which uses formatTime(). */
[[nodiscard]] juce::String defaultLabel(double position, double sampleRate, double majorSpacingSamples);

}

}

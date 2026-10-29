#pragma once

#include <jwaveform/core/jwaveform_SampleRange.h>

namespace jtimeline
{

using jwaveform::SamplePosition;
using jwaveform::SampleRange;

//==============================================================================
/**
    Maps timeline sample positions to horizontal pixel positions relative to the left edge of a view.

    The view's left edge is a sample position plus a fraction of a sample in [0, 1), so that zooming
    stays anchored exactly even when a sample is many pixels wide. Pixel positions are computed
    relative to the view start, so they stay small and precise however long the timeline is.
*/
class TimeMapping
{
public:
    /** Creates a mapping with one sample per pixel and the view starting at sample 0. */
    TimeMapping() = default;
    /** Creates a mapping; a non-positive or non-finite samplesPerPixel becomes 1, and the fraction is normalised. */
    TimeMapping(double samplesPerPixel, SamplePosition startSample, double startFraction = 0.0);

    /** Returns the zoom as samples per pixel; below 1 when zoomed in past one sample per pixel. */
    [[nodiscard]] double getSamplesPerPixel() const { return samplesPerPixel; }
    /** Returns the zoom as pixels per sample. */
    [[nodiscard]] double getPixelsPerSample() const { return 1.0 / samplesPerPixel; }
    /** Returns the whole sample at or before the view's left edge. */
    [[nodiscard]] SamplePosition getStartSample() const { return startSample; }
    /** Returns how far past getStartSample() the left edge lies, in [0, 1). */
    [[nodiscard]] double getStartFraction() const { return startFraction; }

    /** Returns the pixel position of a sample position's left edge. */
    [[nodiscard]] double toPixel(SamplePosition position) const;
    /** Returns the width in pixels of a number of samples. */
    [[nodiscard]] double toPixelLength(SamplePosition length) const;
    /** Returns the sample under a pixel position: the sample whose span contains it. */
    [[nodiscard]] SamplePosition sampleAt(double pixel) const;
    /** Returns the sample nearest a pixel position, rounding to the closest sample boundary. */
    [[nodiscard]] SamplePosition nearestSampleTo(double pixel) const;

    /**
        Returns a pixel position suitable for graphics: toPixel(position) clamped to
        [-margin, width + margin], so that coordinates stay small enough for float.
    */
    [[nodiscard]] float toGraphicsX(SamplePosition position, double width, double margin = 2.0) const;

    /** Returns the samples at least partly visible in a view of the given width. */
    [[nodiscard]] SampleRange visibleRange(double width) const;

    /** Returns this mapping with a new zoom, keeping the position under anchorPixel where it is. */
    [[nodiscard]] TimeMapping zoomedAround(double newSamplesPerPixel, double anchorPixel) const;
    /** Returns this mapping scrolled so that the view content moves left by the given pixels. */
    [[nodiscard]] TimeMapping scrolledBy(double pixels) const;
    /** Returns this mapping scrolled so that the sample position's left edge is at the pixel. */
    [[nodiscard]] TimeMapping withSampleAt(SamplePosition position, double pixel) const;

    /** Compares two mappings. */
    friend bool operator==(const TimeMapping&, const TimeMapping&) = default;

private:
    [[nodiscard]] TimeMapping withStartOffsetFrom(SamplePosition origin, double offset) const;

    double samplesPerPixel = 1.0;
    SamplePosition startSample = 0;
    double startFraction = 0.0;
};

//==============================================================================
/** The allowed range of zoom, in samples per pixel. */
struct ZoomLimits
{
    /** The default deepest zoom: 16 pixels per sample. */
    static constexpr double defaultMinSamplesPerPixel = 1.0 / 16.0;
    /** The shortest length, in seconds, a timeline is treated as having for zooming out. */
    static constexpr double minimumTimelineSeconds = 10.0;

    double minSamplesPerPixel = defaultMinSamplesPerPixel; /**< The deepest zoom. */
    double maxSamplesPerPixel = 1.0; /**< The widest zoom; never below minSamplesPerPixel. */

    /**
        Returns the limits for a timeline: zooming out stops when the whole timeline (at least
        minimumTimelineSeconds long) fits the view width.
    */
    [[nodiscard]] static ZoomLimits forTimeline(double sampleRate, SamplePosition length, double viewWidth, double minSamplesPerPixel = defaultMinSamplesPerPixel);

    /** Returns the zoom clamped to the limits. */
    [[nodiscard]] double clamp(double samplesPerPixel) const;
};

//==============================================================================
/**
    The timing of a clip: where it sits on the timeline and which region of its source it plays.
    Source positions are in timeline samples, since every source runs at the timeline rate.
*/
struct ClipTiming
{
    SamplePosition start = 0; /**< The clip's first timeline sample. */
    SamplePosition length = 1; /**< The clip's length in samples; at least 1 for a valid clip. */
    SamplePosition sourceOffset = 0; /**< The source sample played at the clip's start. */

    /** Returns one past the clip's last timeline sample. */
    [[nodiscard]] constexpr SamplePosition end() const { return start + length; }
    /** Returns the clip's timeline span. */
    [[nodiscard]] constexpr SampleRange timelineRange() const { return { .start = start, .end = end() }; }
    /** Returns the source region the clip plays. */
    [[nodiscard]] constexpr SampleRange sourceRange() const { return { .start = sourceOffset, .end = sourceOffset + length }; }

    /** Returns the source sample played at a timeline position. */
    [[nodiscard]] constexpr SamplePosition toSource(SamplePosition timelinePosition) const { return sourceOffset + (timelinePosition - start); }
    /** Returns the timeline position at which a source sample plays. */
    [[nodiscard]] constexpr SamplePosition toTimeline(SamplePosition sourcePosition) const { return start + (sourcePosition - sourceOffset); }

    /** Returns the source samples played during the part of a timeline range that the clip covers. */
    [[nodiscard]] constexpr SampleRange sourceRangeFor(SampleRange timelineRange) const
    {
        const auto covered = this->timelineRange().intersection(timelineRange);
        if (covered.isEmpty())
            return { .start = sourceOffset, .end = sourceOffset };
        return { .start = toSource(covered.start), .end = toSource(covered.end) };
    }

    /** Compares two timings. */
    friend constexpr bool operator==(const ClipTiming&, const ClipTiming&) = default;
};

}

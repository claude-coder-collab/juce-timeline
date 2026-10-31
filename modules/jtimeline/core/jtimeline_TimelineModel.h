#pragma once

#include "jtimeline_Ids.h"
#include "jtimeline_TimeMapping.h"

#include <juce_graphics/juce_graphics.h>
#include <jwaveform/core/jwaveform_PeakCache.h>

#include <functional>
#include <optional>
#include <vector>

namespace jtimeline
{

class TimelineNotifier;

/** What a track looks like. Unset fields use the view's defaults. */
struct TrackInfo
{
    juce::String name; /**< Shown in the track header. */
    std::optional<juce::Colour> colour; /**< The track's colour, used for its header and coverage strips. */
    std::optional<int> heightHint; /**< The height a view first gives the track, in pixels. */
};

/** The kind of a clip. */
enum class ClipKind {
    audio, /**< A region of an audio source. */
    group /**< Stands for a group of clips the model keeps elsewhere; never has a waveform or fades. */
};

/** Everything a view needs to draw a clip. */
struct ClipInfo
{
    ClipKind kind = ClipKind::audio; /**< Audio or group. */
    TrackId track{}; /**< The track holding the clip. */
    SamplePosition start = 0; /**< The first timeline sample. */
    SamplePosition length = 1; /**< The length in samples; a reported 0 is treated as 1. */
    SamplePosition sourceOffset = 0; /**< The source sample played at the start. */
    std::optional<SamplePosition> sourceLength; /**< The source's length, if known. */
    juce::String name; /**< Shown on the clip. */
    std::optional<juce::Colour> colour; /**< The clip colour; unset uses the track's. */
    double gainDb = 0.0; /**< Display-only gain; 0 is unity. */
    SamplePosition fadeIn = 0; /**< Fade-in length in samples. */
    SamplePosition fadeOut = 0; /**< Fade-out length in samples. */
    bool muted = false; /**< Muted clips are dimmed. */
    std::optional<jwaveform::WaveformSource> source; /**< The waveform to draw, if any. */

    /** Returns the clip's timeline span, at least one sample long. */
    [[nodiscard]] SampleRange range() const { return { .start = start, .end = start + std::max<SamplePosition>(length, 1) }; }
};

/** A named position on the timeline, drawn on the ruler. */
struct Marker
{
    MarkerId id{}; /**< The marker's identity. */
    SamplePosition position = 0; /**< The marked sample. */
    juce::String name; /**< Shown next to the marker. */
    std::optional<juce::Colour> colour; /**< The marker colour; unset uses Timeline::markerColourId. */
};

/** Which way adjacentClip() looks. */
enum class Direction {
    previous, /**< Earlier in start order. */
    next /**< Later in start order. */
};

//==============================================================================
/**
    Connects a Timeline to your data, as a set of callbacks.

    A TimelineModel is a plain struct of std::function members, so your data needs no base class
    and the Timeline is not a template. Every callback is called on the message thread, and only
    for what a view shows, except clipsInRange over the whole timeline for select-all.

    A model missing any of trackIds, trackInfo, clipsInRange, clipInfo, sampleRate or length shows
    as empty.
*/
struct TimelineModel
{
    /** Returns the tracks, in display order. */
    std::function<std::vector<TrackId>()> trackIds;
    /** Returns a track's info, or nothing if it does not exist. */
    std::function<std::optional<TrackInfo>(TrackId)> trackInfo;
    /**
        Returns the clips on a track with start < rangeEnd and start + length > rangeStart, in
        draw order (later clips on top). Views always pass 0 <= rangeStart < rangeEnd <= length().
        Answer without scanning the whole track, e.g. from a sorted index.
    */
    std::function<std::vector<ClipId>(TrackId, SamplePosition rangeStart, SamplePosition rangeEnd)> clipsInRange;
    /** Returns a clip's info, or nothing if it does not exist. */
    std::function<std::optional<ClipInfo>(ClipId)> clipInfo;
    /** Returns the timeline sample rate. */
    std::function<double()> sampleRate;
    /** Returns the timeline end, in samples. */
    std::function<SamplePosition()> length;
    /** Optional: returns the previous or next clip on a track in start order, ties by draw order. */
    std::function<std::optional<ClipId>(TrackId, ClipId, Direction)> adjacentClip;
    /**
        Returns the occupied spans of a track within a range, sorted by start, merging clips that
        overlap or are less than minGap samples apart. Coverage drawing uses it; without it,
        coverage is computed from clipsInRange and clipInfo.
    */
    std::function<std::vector<SampleRange>(TrackId, SamplePosition rangeStart, SamplePosition rangeEnd, SamplePosition minGap)> clipSpans;
    /**
        Optional: returns the markers with rangeStart <= position < rangeEnd, sorted by position.
        Views pass ranges a little wider than they show, and may pass ranges outside [0, length()].
    */
    std::function<std::vector<Marker>(SamplePosition rangeStart, SamplePosition rangeEnd)> markersInRange;
    /** Optional: returns the timeline selection, or nothing if there is none. */
    std::function<std::optional<SampleRange>()> timelineSelection;

    /** The notifier the model reports changes through. Without one, call Timeline::refresh(). */
    TimelineNotifier* notifier = nullptr;

    /** Returns true if every required callback is set. */
    [[nodiscard]] bool isComplete() const { return trackIds && trackInfo && clipsInRange && clipInfo && sampleRate && length; }
};

/** Sorts spans by start and merges those that overlap or are less than minGap apart; for implementing clipSpans. */
[[nodiscard]] std::vector<SampleRange> mergeSpans(std::vector<SampleRange> spans, SamplePosition minGap);

}

#pragma once

#include "../core/jtimeline_ClipWaveform.h"
#include "../core/jtimeline_LevelOfDetail.h"
#include "../core/jtimeline_TimelineModel.h"
#include "../core/jtimeline_TrackLayout.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace jtimeline
{

class Timeline;
class TrackHeader;

/** Sizes a Timeline lays itself out with. */
struct TimelineMetrics
{
    int rulerHeight = 28; /**< The time ruler's height. */
    int headerWidth = 160; /**< The track headers' width. */
    int scrollBarThickness = 12; /**< The scroll bars' thickness. */
    int defaultTrackHeight = 72; /**< The height of tracks with no height set or hinted. */
    std::vector<int> trackHeightSteps{ trackHeights::defaultSteps.begin(), trackHeights::defaultSteps.end() }; /**< Keyboard track height steps, ascending. */
    DetailThresholds detail; /**< Clip widths for each level of detail. */
    float clipInset = 1.0f; /**< Space between a clip and its lane's top and bottom edges. */
    float minWaveformHeight = 12.0f; /**< Clips shorter than this draw no waveform, and stacked channels shorter than this are combined. */
    int markerStripHeight = 18; /**< The strip above the ruler's time scale for markers and the timeline selection, shown when the model has either. */
};

/** A clip's waveform as the Timeline asks the look and feel to draw it. */
struct ClipWaveform
{
    std::shared_ptr<const jwaveform::PeakData> peaks; /**< The peaks, possibly partial; null until a build has opened the source. */
    WaveformStatus status = WaveformStatus::none; /**< What the clip can show. */
    double sourceStart = 0.0; /**< The source position at the left edge of ClipPaint::bounds. */
    double sourceEnd = 0.0; /**< The source position at the right edge of ClipPaint::bounds. */
    double scale = 1.0; /**< The waveform vertical scale of the clip's track. */
    jwaveform::WaveformEnvelope envelope; /**< The clip's gain and fades. */
    jwaveform::RawSamples samples; /**< Raw samples for the span, at zooms finer than the peaks; empty otherwise or until they arrive. */
};

/** A clip as the Timeline asks the look and feel to draw it. */
struct ClipPaint
{
    ClipId id{}; /**< The clip. */
    const ClipInfo* info = nullptr; /**< The clip's info; never null. */
    juce::Rectangle<float> bounds; /**< The clip's bounds, cut a few pixels outside the visible area. */
    bool cutLeft = false; /**< The clip continues past the left of bounds. */
    bool cutRight = false; /**< The clip continues past the right of bounds. */
    ClipTier tier = ClipTier::full; /**< The level of detail; never coverage. */
    juce::Colour colour; /**< The clip's colour, or its track's, or the default clip colour. */
    double startX = 0.0; /**< The clip's uncut left edge, which may lie far outside bounds. */
    double endX = 0.0; /**< The clip's uncut right edge, which may lie far outside bounds. */
    ClipWaveform waveform; /**< The waveform; its status is none when the clip draws without one. */
    bool selected = false; /**< The clip is selected. */
    bool cursor = false; /**< The keyboard cursor is on the clip and the timeline has keyboard focus. */
};

/** A marker as the Timeline asks the look and feel to draw it. */
struct MarkerPaint
{
    float x = 0.0f; /**< The marker position. */
    const Marker* marker = nullptr; /**< The marker; never null. */
    juce::Colour colour; /**< The marker's colour, or the default marker colour. */
};

/** A ruler tick as the Timeline asks the look and feel to draw it. */
struct RulerMark
{
    float x = 0.0f; /**< The tick position. */
    bool major = false; /**< Major ticks are taller and labelled. */
    juce::String label; /**< The label of a major tick. */
};

//==============================================================================
/**
    Drawing and metrics for Timeline, following JUCE's LookAndFeelMethods convention: derive your
    LookAndFeel from this class as well and override what you need. A Timeline whose look and
    feel does not derive from it uses these defaults. Colours come from Timeline::ColourIds.
*/
class TimelineLookAndFeelMethods
{
public:
    TimelineLookAndFeelMethods() = default;
    virtual ~TimelineLookAndFeelMethods() = default;
    /** @internal */
    TimelineLookAndFeelMethods(const TimelineLookAndFeelMethods&) = default;
    /** @internal */
    TimelineLookAndFeelMethods& operator=(const TimelineLookAndFeelMethods&) = default;
    /** @internal */
    TimelineLookAndFeelMethods(TimelineLookAndFeelMethods&&) = default;
    /** @internal */
    TimelineLookAndFeelMethods& operator=(TimelineLookAndFeelMethods&&) = default;

    /** Returns the sizes the timeline lays itself out with. */
    virtual TimelineMetrics getTimelineMetrics(const Timeline& timeline);

    /** Draws a track lane's background. */
    virtual void drawTimelineLane(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> lane, TrackId track, const TrackInfo& info);
    /** Draws one clip at the full, reduced or minimal tier, calling the waveform and fade methods. */
    virtual void drawTimelineClip(juce::Graphics& g, const Timeline& timeline, const ClipPaint& clip);
    /** Draws a clip's waveform, or its failure state, in an area spanning the clip's bounds horizontally. */
    virtual void drawTimelineClipWaveform(juce::Graphics& g, const Timeline& timeline, const ClipPaint& clip, juce::Rectangle<float> area);
    /** Draws a full-tier clip's fade-in and fade-out over its body. */
    virtual void drawTimelineClipFades(juce::Graphics& g, const Timeline& timeline, const ClipPaint& clip, juce::Rectangle<float> body);
    /** Draws a track's coverage strip: the spans occupied by clips too narrow to draw alone. */
    virtual void drawTimelineCoverage(juce::Graphics& g, const Timeline& timeline, const juce::RectangleList<float>& spans, juce::Colour colour);
    /** Draws the time ruler. */
    virtual void drawTimelineRuler(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> area, std::span<const RulerMark> marks);
    /** Draws the playhead in the clip area (inRuler false) or the ruler (inRuler true). */
    virtual void drawTimelinePlayhead(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> area, float x, bool inRuler);
    /** Draws the strip above the time scale: its background, the timeline selection (if any) and the markers, with names to their right. */
    virtual void drawTimelineMarkerStrip(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> area, std::span<const MarkerPaint> markers, std::optional<juce::Rectangle<float>> timelineSelection);
    /** Draws the time selection over the clip area (inRuler false) or the ruler's time scale (inRuler true). */
    virtual void drawTimelineTimeSelection(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> area, bool inRuler);
    /** Draws the rubber band while dragging a selection rectangle in the clip area. */
    virtual void drawTimelineRubberBand(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> area);
    /** Draws an overview's highlighted range. */
    virtual void drawTimelineHighlight(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> range);
    /** Draws a default track header, showing whether the track is selected or has the keyboard cursor. */
    virtual void drawTimelineTrackHeader(juce::Graphics& g, const TrackHeader& header);

    /** Returns the methods of a component's look and feel, or the defaults. */
    [[nodiscard]] static TimelineLookAndFeelMethods& forComponent(const juce::Component& component);
};

}

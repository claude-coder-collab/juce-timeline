#pragma once

#include "../core/jtimeline_LevelOfDetail.h"
#include "../core/jtimeline_TimelineModel.h"
#include "../core/jtimeline_TrackLayout.h"

#include <juce_gui_basics/juce_gui_basics.h>

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
    /** Draws one clip at the full, reduced or minimal tier. */
    virtual void drawTimelineClip(juce::Graphics& g, const Timeline& timeline, const ClipPaint& clip);
    /** Draws a track's coverage strip: the spans occupied by clips too narrow to draw alone. */
    virtual void drawTimelineCoverage(juce::Graphics& g, const Timeline& timeline, const juce::RectangleList<float>& spans, juce::Colour colour);
    /** Draws the time ruler. */
    virtual void drawTimelineRuler(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> area, std::span<const RulerMark> marks);
    /** Draws the playhead in the clip area (inRuler false) or the ruler (inRuler true). */
    virtual void drawTimelinePlayhead(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> area, float x, bool inRuler);
    /** Draws an overview's highlighted range. */
    virtual void drawTimelineHighlight(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> range);
    /** Draws a default track header. */
    virtual void drawTimelineTrackHeader(juce::Graphics& g, const TrackHeader& header);

    /** Returns the methods of a component's look and feel, or the defaults. */
    [[nodiscard]] static TimelineLookAndFeelMethods& forComponent(const juce::Component& component);
};

}

#pragma once

#include "../core/jtimeline_KeyboardNavigation.h"
#include "../core/jtimeline_RulerTicks.h"
#include "../core/jtimeline_Selection.h"
#include "../core/jtimeline_TimelineNotifier.h"
#include "jtimeline_TimelineLookAndFeel.h"
#include "jtimeline_TrackHeader.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace jtimeline
{

namespace detail
{
class ClipArea;
class HeaderArea;
class Ruler;
}

//==============================================================================
/**
    A view of tracks and clips on a time axis, bound to a TimelineModel.

    The view scrolls virtually: zoom is samples per pixel and the left edge is a sample position
    plus a fraction, so any timeline length works at any zoom. It reads only visible tracks and
    clips from the model and updates on TimelineNotifier changes.

    Wheel, trackpad and key bindings follow Pro Tools' defaults (see keys::actionFor and
    wheel::actionFor). Horizontal zooms not anchored at the pointer are anchored at the edit
    cursor: the start of the time selection, else the playhead.

    Clips and tracks are selected by clicking (Shift adds, Command/Ctrl toggles), by dragging a
    rubber band over empty space, by Command/Ctrl + A and by keyboard navigation; a drag on the
    ruler selects a time range. Selection is view state, held by id and kept across model
    changes while the ids exist. Visible clips are exposed to accessibility clients.

    Markers and a timeline selection, if the model supplies them, are drawn in a strip above the
    ruler. The timeline selection is model data, unlike the view's time selection.

    Clips with a WaveformSource draw their waveforms through the PeakCache set with setPeakCache(),
    requesting peaks only for clips drawn with room for a waveform and releasing them when no such
    clip is drawn any more. Partial peaks are drawn while they build.

    Colours are set with setColour() using ColourIds on the timeline, an ancestor or the look and
    feel; drawing and metrics can be replaced through TimelineLookAndFeelMethods.

    All methods must be called on the message thread.
*/
class Timeline : public juce::Component, private TimelineNotifier::Listener, private juce::ScrollBar::Listener, private juce::Timer
{
public:
    /** Colour ids for setColour(). */
    enum ColourIds // NOLINT(cppcoreguidelines-use-enum-class)
    {
        backgroundColourId = 0x2a52000, /**< Behind tracks and past the last track. Falls back to ResizableWindow::backgroundColourId. */
        laneColourId = 0x2a52001, /**< Track lane background. Falls back to a shade of the background. */
        separatorColourId = 0x2a52002, /**< Lines between tracks. */
        headerBackgroundColourId = 0x2a52003, /**< Track headers. */
        headerTextColourId = 0x2a52004, /**< Track header names. */
        rulerBackgroundColourId = 0x2a52005, /**< The time ruler. */
        rulerTextColourId = 0x2a52006, /**< Ruler labels and ticks. */
        clipColourId = 0x2a52007, /**< Clips with no clip or track colour. */
        clipTextColourId = 0x2a52008, /**< Clip names. */
        clipOutlineColourId = 0x2a52009, /**< Clip outlines. */
        playheadColourId = 0x2a5200a, /**< The playhead. */
        highlightColourId = 0x2a5200b, /**< An overview's highlighted range. */
        waveformColourId = 0x2a5200c, /**< Clip waveforms. Falls back to the clip text colour. */
        fadeColourId = 0x2a5200d, /**< The shading above clip fade lines. */
        selectionColourId = 0x2a5200e, /**< Selected clip outlines, rubber bands and the tint of selected tracks. */
        timeSelectionColourId = 0x2a5200f, /**< The time selection. */
        markerColourId = 0x2a52010, /**< Markers with no colour of their own. */
        timelineSelectionColourId = 0x2a52011, /**< The model's timeline selection. */
        keyboardCursorColourId = 0x2a52012 /**< The keyboard cursor, shown while the timeline has keyboard focus. */
    };

    /** Creates an empty timeline. Call setModel() to show data. */
    Timeline();
    /** Destructor. */
    ~Timeline() override;

    Timeline(const Timeline&) = delete;
    Timeline& operator=(const Timeline&) = delete;
    Timeline(Timeline&&) = delete;
    Timeline& operator=(Timeline&&) = delete;

    //==============================================================================
    /**
        Binds a model, replacing any previous one, and subscribes to its notifier. Pass an empty
        TimelineModel to unbind. Everything the callbacks capture must outlive the binding.
    */
    void setModel(TimelineModel newModel);
    /** Returns the bound model. */
    [[nodiscard]] const TimelineModel& getModel() const { return model; }
    /** Re-reads everything from the model, as a FullReset does. Models with a notifier never need this. */
    void refresh();

    /** Sets the cache that builds clip waveforms; several timelines can share one. Without one, only ready-made peaks are drawn. */
    void setPeakCache(std::shared_ptr<jwaveform::PeakCache> cache);
    /** Returns the peak cache, or nullptr. */
    [[nodiscard]] const std::shared_ptr<jwaveform::PeakCache>& getPeakCache() const { return peakCache; }

    //==============================================================================
    /** Returns the time mapping of the clip area. */
    [[nodiscard]] const TimeMapping& getMapping() const { return mapping; }
    /** Sets the zoom and horizontal position, clamped to the zoom limits and scroll range. */
    void setMapping(const TimeMapping& newMapping);
    /**
        Returns the zoom limits for the current model and width. The deepest zoom is one level-0
        peak (the cache's samples per peak) per physical pixel.
    */
    [[nodiscard]] ZoomLimits getZoomLimits() const;
    /** Returns the samples visible in the clip area. */
    [[nodiscard]] SampleRange getVisibleRange() const;
    /** Zooms and scrolls so a range fills the clip area, within the zoom limits. */
    void setVisibleRange(SampleRange range);
    /** Zooms out to show the whole timeline. */
    void zoomToFit();
    /** Zooms horizontally by a number of steps of a factor, anchored at the edit cursor; negative steps zoom out. */
    void zoomAtEditCursor(double steps, double factorPerStep);
    /** Scrolls so a position is at the left edge, as far as the scroll range allows. */
    void scrollTo(SamplePosition position);

    /** Returns the vertical scroll position of the tracks, in pixels. */
    [[nodiscard]] int getTrackScroll() const { return trackScroll; }
    /** Scrolls the tracks vertically, clamped to their height. */
    void setTrackScroll(int pixels);

    //==============================================================================
    /** Returns the tracks, in display order, as last read from the model. */
    [[nodiscard]] std::vector<TrackId> getTrackIds() const;
    /** Returns a track's height: set, else hinted by the model, else the default. */
    [[nodiscard]] int getTrackHeight(TrackId track) const;
    /** Sets a track's height in this view; nothing clears it but setDefaultTrackHeight(). */
    void setTrackHeight(TrackId track, int height);
    /** Sets the height of tracks with no height set or hinted, and clears every set height. */
    void setDefaultTrackHeight(int height);
    /** Moves tracks' heights to the next look-and-feel height step up (direction > 0) or down. */
    void stepTrackHeights(std::span<const TrackId> tracksToStep, int direction);

    //==============================================================================
    /** Moves the playhead. Only the old and new playhead strips are repainted. */
    void setPlayhead(SamplePosition position);
    /** Returns the playhead. */
    [[nodiscard]] SamplePosition getPlayhead() const { return playhead; }
    /** In page follow mode, a playhead leaving the visible range scrolls the view so it is at the left edge. */
    void setFollowPlayhead(bool shouldFollow);
    /** Returns true in page follow mode. */
    [[nodiscard]] bool isFollowingPlayhead() const { return followPlayhead; }
    /** Returns the edit cursor, the anchor of zooms not at the pointer: the time selection's start, else the playhead. */
    [[nodiscard]] SamplePosition getEditCursor() const;

    //==============================================================================
    /** Returns the selection. */
    [[nodiscard]] const Selection& getSelection() const { return selection; }
    /** Returns the selected clips, sorted by id. */
    [[nodiscard]] std::vector<ClipId> getSelectedClips() const;
    /** Returns the selected tracks, in display order. */
    [[nodiscard]] std::vector<TrackId> getSelectedTracks() const;
    /** Returns true if a clip is selected. */
    [[nodiscard]] bool isClipSelected(ClipId clip) const { return selection.contains(clip); }
    /** Returns true if a track is selected. */
    [[nodiscard]] bool isTrackSelected(TrackId track) const { return selection.contains(track); }
    /** Returns the selected time range, if any. */
    [[nodiscard]] std::optional<SampleRange> getTimeSelection() const { return selection.getTimeRange(); }
    /** Replaces the selected clips. */
    void setSelectedClips(std::span<const ClipId> clips);
    /** Replaces the selected tracks. */
    void setSelectedTracks(std::span<const TrackId> selectedTracks);
    /** Sets the time selection, clamped to the timeline; empty clears it. */
    void setTimeSelection(std::optional<SampleRange> range);
    /** Selects a clip as a click would, and moves the keyboard cursor to it. */
    void selectClip(ClipId clip, SelectMode mode);
    /** Selects a track as a click on its header would, and moves the keyboard cursor to it. */
    void selectTrack(TrackId track, SelectMode mode);
    /** Selects every clip, querying each track over the whole timeline. */
    void selectAllClips();
    /** Deselects every clip and track and clears the time selection. */
    void clearSelection();
    /** Zooms and scrolls to the time selection, else to the span of the selected clips. */
    void zoomToSelection();

    /** Returns the keyboard cursor. */
    [[nodiscard]] const KeyboardCursor& getKeyboardCursor() const { return keyboardCursor; }
    /** Moves the keyboard cursor without changing the selection or scrolling. A clip's track is filled in from the model. */
    void setKeyboardCursor(KeyboardCursor cursor);
    /**
        Moves the keyboard cursor as the arrow, Return and Escape keys do, selects what it lands on
        and scrolls it into view. Returns false if the cursor did not move.
    */
    bool moveKeyboardCursor(CursorMove move, SelectMode mode);

    /** Returns the timeline selection as last read from the model. */
    [[nodiscard]] std::optional<SampleRange> getTimelineSelection() const { return timelineSelection; }

    //==============================================================================
    /** Sets the waveform vertical scale, clamped to [0.25, 64]. Drawing only. */
    void setWaveformScale(double scale);
    /** Returns the waveform vertical scale. */
    [[nodiscard]] double getWaveformScale() const { return waveformScale; }
    /** Overrides the waveform scale for one track; empty uses the view's. */
    void setTrackWaveformScale(TrackId track, std::optional<double> scale);
    /** Returns the waveform scale a track draws with. */
    [[nodiscard]] double getWaveformScaleFor(TrackId track) const;

    //==============================================================================
    /** Replaces the ruler labels; an empty formatter restores h:mm:ss.mmm. */
    void setRulerFormatter(RulerFormatter formatter);
    /** Sets the factory for track headers, called only when the header pool needs another; empty restores the default header. */
    void setTrackHeaderFactory(std::function<std::unique_ptr<TrackHeader>()> factory);

    //==============================================================================
    /**
        In overview mode the timeline always shows the whole timeline with every track squeezed
        into its height, in coverage form only, with no headers, ruler, scroll bars or per-clip
        queries. Clicks and drags report positions through onTimeClicked.
    */
    void setOverviewMode(bool shouldBeOverview);
    /** Returns true in overview mode. */
    [[nodiscard]] bool isOverviewMode() const { return overview; }
    /** Sets the range an overview highlights, typically another view's visible range. */
    void setHighlightedRange(SampleRange range);

    //==============================================================================
    /** Returns the track at a y position in timeline coordinates, if any. */
    [[nodiscard]] std::optional<TrackId> trackAt(float y) const;
    /** Returns the topmost clip at a point in timeline coordinates, if any. */
    [[nodiscard]] std::optional<ClipId> clipAt(juce::Point<float> position) const;
    /** Returns the clip area's bounds in timeline coordinates. */
    [[nodiscard]] juce::Rectangle<int> getClipAreaBounds() const;

    /** Returns a colour as the timeline paints it. */
    [[nodiscard]] juce::Colour resolveColour(int colourId) const;

    //==============================================================================
    /** Called with the time position of a click or drag on the ruler, or anywhere in overview mode. */
    std::function<void(SamplePosition position)> onTimeClicked; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
    /** Called when the zoom, horizontal position or vertical scroll changes. */
    std::function<void()> onViewChanged; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
    /** Called when the view's or a track's waveform scale changes. */
    std::function<void()> onWaveformScaleChanged; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
    /** Called when the selected clips, tracks or time range change, whatever the cause. */
    std::function<void()> onSelectionChanged; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)

    /** @internal */
    void resized() override;
    /** @internal */
    void paint(juce::Graphics& g) override;
    /** @internal */
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    /** @internal */
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;
    /** @internal */
    bool keyPressed(const juce::KeyPress& key) override;
    /** @internal */
    void lookAndFeelChanged() override;
    /** @internal */
    void colourChanged() override;
    /** @internal */
    void focusGained(FocusChangeType cause) override;
    /** @internal */
    void focusLost(FocusChangeType cause) override;
    /** @internal */
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    /** @internal Takes keyboard focus after a click on the timeline or one of its parts, if it is showing. */
    void focusFromClick();
    /** @internal Returns a clip's accessible description: its track, time range and state. */
    [[nodiscard]] juce::String describeClip(const ClipInfo& info) const;

private:
    friend class detail::ClipArea;
    friend class detail::HeaderArea;
    friend class detail::Ruler;

    struct Track
    {
        TrackId id{};
        TrackInfo info;
    };

    struct TrackClips
    {
        SampleRange queried;
        double samplesPerPixel = 0.0;
        bool dense = false;
        std::vector<ClipId> order;
        std::vector<SampleRange> spans;
    };

    void modelChanged(const Change& change) override;
    void notifierDestroyed(TimelineNotifier& destroyedNotifier) override;
    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
    void timerCallback() override;

    void readTracks();
    void readTimeline();
    void clipsChanged(std::span<const ClipId> ids);
    void layoutChanged();
    void viewChanged();
    void updateScrollBars();
    [[nodiscard]] TimeMapping clamped(const TimeMapping& candidate) const;
    [[nodiscard]] std::pair<double, double> scrollLimits(double samplesPerPixel) const;
    [[nodiscard]] TimeMapping overviewMapping() const;
    [[nodiscard]] double clipAreaWidth() const;
    [[nodiscard]] int clipAreaHeight() const;
    [[nodiscard]] TimelineMetrics metrics() const;
    [[nodiscard]] double timelineExtent() const;
    void repaintPlayhead(SamplePosition position);
    void repaintClip(const ClipInfo& info);
    [[nodiscard]] juce::Rectangle<int> clipRect(const ClipInfo& info) const;
    [[nodiscard]] double physicalPixelScale() const;

    ClipWaveform waveformFor(const ClipInfo& info, const jwaveform::WaveformSource& source);
    void waveformChanged(const juce::String& key);
    void releaseUnusedWaveforms();

    void selectionChanged();
    void validateSelection(std::optional<std::span<const ClipId>> changedClips);
    void validateCursor();
    void cursorChanged();
    [[nodiscard]] NavigationContext navigationContext() const;
    void reveal(const KeyboardCursor& cursor);
    void revealTrack(int index);
    [[nodiscard]] std::vector<ClipId> clipsInArea(juce::Rectangle<float> area) const;
    [[nodiscard]] juce::String describe(const KeyboardCursor& cursor) const;
    [[nodiscard]] juce::String trackName(TrackId track) const;
    static void announce(const juce::String& text);

    [[nodiscard]] bool hasMarkerStrip() const;
    [[nodiscard]] int rulerTotalHeight() const;
    const std::vector<Marker>& markersFor(SampleRange range);
    void readMarkers();

    [[nodiscard]] int trackIndex(TrackId track) const;
    [[nodiscard]] juce::Rectangle<float> laneBounds(int index) const;
    [[nodiscard]] const ClipInfo* cachedClip(ClipId id) const;
    const TrackClips& clipsFor(int index, SampleRange visible);
    [[nodiscard]] std::vector<SampleRange> spansFor(TrackId track, SampleRange range, SamplePosition minGap) const;
    [[nodiscard]] std::optional<juce::Colour> specifiedColour(int colourId) const;
    [[nodiscard]] juce::Colour colourFor(const ClipInfo& info) const;
    [[nodiscard]] juce::Colour colourFor(const Track& track) const;

    TimelineModel model;
    TimelineNotifier* notifier = nullptr;
    std::shared_ptr<jwaveform::PeakCache> peakCache;

    struct Waveform
    {
        jwaveform::PeakHandle handle;
        bool used = false;
    };
    std::unordered_map<juce::String, Waveform> waveforms;
    std::unordered_set<juce::String> changedWaveforms;
    double sampleRate = 48000.0;
    SamplePosition length = 0;

    std::vector<Track> tracks;
    std::unordered_map<TrackId, int> trackIndices;
    TrackLayout layout;
    std::unordered_map<TrackId, int> setHeights;
    int defaultTrackHeight = 0;

    std::unordered_map<TrackId, TrackClips> trackClips;
    std::unordered_map<ClipId, ClipInfo> clipInfos;
    std::unordered_map<TrackId, std::vector<SampleRange>> overviewSpans;
    double overviewSamplesPerPixel = 0.0;

    TimeMapping mapping;
    int trackScroll = 0;
    bool fitted = false;
    SamplePosition playhead = 0;
    bool followPlayhead = false;
    double waveformScale = 1.0;
    std::unordered_map<TrackId, double> trackWaveformScales;
    RulerFormatter rulerFormatter;
    bool overview = false;
    SampleRange highlighted;
    std::function<std::unique_ptr<TrackHeader>()> headerFactory;

    Selection selection;
    KeyboardCursor keyboardCursor;
    std::optional<SampleRange> timelineSelection;
    struct MarkerCache
    {
        bool valid = false;
        SampleRange queried;
        std::vector<Marker> markers;
    };
    MarkerCache markerCache;

    std::unique_ptr<detail::Ruler> ruler;
    std::unique_ptr<detail::HeaderArea> headers;
    std::unique_ptr<detail::ClipArea> clipArea;
    juce::ScrollBar horizontalBar{ false };
    juce::ScrollBar verticalBar{ true };
};

}

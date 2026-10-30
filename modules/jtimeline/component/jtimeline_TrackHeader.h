#pragma once

#include "../core/jtimeline_TimelineModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jtimeline
{

class Timeline;

//==============================================================================
/**
    A track header: by default the track's name and colour, drawn by the look and feel.

    Timelines keep headers only for visible tracks, in a pool, and rebind them to other tracks as
    tracks scroll in and out. To add controls such as mute and solo buttons, derive from this
    class, override trackChanged() to update them for the bound track, and pass a factory to
    Timeline::setTrackHeaderFactory().

    Clicking a header selects its track. Headers are the tracks' accessibility elements: they are
    titled with the track name, report whether the track is selected, and select it on press.
*/
class TrackHeader : public juce::Component
{
public:
    /** Creates an unbound header. */
    TrackHeader();

    /** Returns the bound track. */
    [[nodiscard]] TrackId getTrackId() const { return trackId; }
    /** Returns the bound track's info. */
    [[nodiscard]] const TrackInfo& getTrackInfo() const { return trackInfo; }
    /** Returns the timeline showing the header, or nullptr. */
    [[nodiscard]] Timeline* getTimeline() const { return timeline; }

    /** Called after the header is bound to another track or its track's info changes. Repaints by default. */
    virtual void trackChanged();

    /** Selects the track: Shift adds it to the selection, Command/Ctrl toggles it. Call this from an override. */
    void mouseDown(const juce::MouseEvent& event) override;
    /** @internal */
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    /** @internal */
    void bind(Timeline& owner, TrackId track, const TrackInfo& info);
    /** @internal */
    void paint(juce::Graphics& g) override;

private:
    Timeline* timeline = nullptr;
    TrackId trackId{};
    TrackInfo trackInfo;
};

}

#include "jtimeline_TrackHeader.h"

#include "jtimeline_JuceInput.h"
#include "jtimeline_TimelineLookAndFeel.h"
#include "jtimeline_TimelineParts.h"

namespace jtimeline
{

TrackHeader::TrackHeader()
{
    setWantsKeyboardFocus(false);
}

void TrackHeader::trackChanged()
{
    repaint();
}

void TrackHeader::bind(Timeline& owner, TrackId track, const TrackInfo& info)
{
    const auto same = timeline == &owner && trackId == track && trackInfo.name == info.name && trackInfo.colour == info.colour && trackInfo.heightHint == info.heightHint;
    timeline = &owner;
    trackId = track;
    trackInfo = info;
    if (!same)
    {
        setTitle(info.name.isNotEmpty() ? info.name : juce::String{ "Track" });
        trackChanged();
    }
}

void TrackHeader::mouseDown(const juce::MouseEvent& event)
{
    if (timeline == nullptr)
        return;
    timeline->focusFromClick();
    timeline->selectTrack(trackId, selectModeFrom(event.mods));
}

std::unique_ptr<juce::AccessibilityHandler> TrackHeader::createAccessibilityHandler()
{
    return std::make_unique<detail::SelectableHandler>(*this, juce::AccessibilityRole::group, [this] { return timeline != nullptr && timeline->isTrackSelected(trackId); }, [this](SelectMode mode) {
        if (timeline != nullptr)
            timeline->selectTrack(trackId, mode); });
}

void TrackHeader::paint(juce::Graphics& g)
{
    TimelineLookAndFeelMethods::forComponent(*this).drawTimelineTrackHeader(g, *this);
}

}

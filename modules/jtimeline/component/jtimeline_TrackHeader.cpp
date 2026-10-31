#include "jtimeline_TrackHeader.h"

#include "jtimeline_JuceInput.h"
#include "jtimeline_TimelineLookAndFeel.h"
#include "jtimeline_TimelineParts.h"

namespace jtimeline
{

TrackHeader::TrackHeader()
{
    setWantsKeyboardFocus(false);
    setFocusContainerType(FocusContainerType::focusContainer);
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
    if (event.mods.isShiftDown())
        timeline->extendTrackSelection(trackId);
    else
        timeline->selectTrack(trackId, selectModeFrom(event.mods));
}

std::unique_ptr<juce::AccessibilityHandler> TrackHeader::createAccessibilityHandler()
{
    const auto isSelected = [this] { return timeline != nullptr && timeline->isTrackSelected(trackId); };
    const auto select = [this](SelectMode mode) {
        if (timeline != nullptr)
            timeline->selectTrack(trackId, mode);
    };
    const auto focused = [this] {
        if (timeline != nullptr)
            timeline->setKeyboardCursor({ .track = trackId, .clip = std::nullopt });
    };
    return std::make_unique<detail::SelectableHandler>(*this, juce::AccessibilityRole::group, isSelected, select, focused);
}

void TrackHeader::paint(juce::Graphics& g)
{
    TimelineLookAndFeelMethods::forComponent(*this).drawTimelineTrackHeader(g, *this);
}

}

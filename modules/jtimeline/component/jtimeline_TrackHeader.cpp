#include "jtimeline_TrackHeader.h"

#include "jtimeline_TimelineLookAndFeel.h"

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
        trackChanged();
}

void TrackHeader::paint(juce::Graphics& g)
{
    TimelineLookAndFeelMethods::forComponent(*this).drawTimelineTrackHeader(g, *this);
}

}

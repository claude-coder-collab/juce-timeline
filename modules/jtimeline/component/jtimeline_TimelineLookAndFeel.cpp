#include "jtimeline_TimelineLookAndFeel.h"

#include "jtimeline_Timeline.h"
#include "jtimeline_TrackHeader.h"

namespace jtimeline
{

namespace
{

constexpr float cornerRadius = 3.0f;
constexpr float bodyAlpha = 0.75f;
constexpr float mutedAlpha = 0.35f;
constexpr float nameHeight = 15.0f;
constexpr float badgeSize = 9.0f;

juce::Font smallFont(float height)
{
    return juce::Font{ juce::FontOptions{ height } };
}

void drawGroupBadge(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
{
    const auto badge = juce::Rectangle<float>{ bounds.getRight() - badgeSize - 4.0f, bounds.getY() + 3.0f, badgeSize, badgeSize };
    if (badge.getX() < bounds.getX() + 2.0f || badge.getBottom() > bounds.getBottom())
        return;
    g.setColour(colour);
    g.drawRect(badge.withTrimmedRight(3.0f).withTrimmedBottom(3.0f), 1.0f);
    g.drawRect(badge.withTrimmedLeft(3.0f).withTrimmedTop(3.0f), 1.0f);
}

}

TimelineMetrics TimelineLookAndFeelMethods::getTimelineMetrics(const Timeline& /*timeline*/)
{
    return {};
}

void TimelineLookAndFeelMethods::drawTimelineLane(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> lane, TrackId /*track*/, const TrackInfo& /*info*/)
{
    g.setColour(timeline.resolveColour(Timeline::laneColourId));
    g.fillRect(lane);
    g.setColour(timeline.resolveColour(Timeline::separatorColourId));
    g.fillRect(lane.withTop(lane.getBottom() - 1.0f));
}

void TimelineLookAndFeelMethods::drawTimelineClip(juce::Graphics& g, const Timeline& timeline, const ClipPaint& clip)
{
    const auto& info = *clip.info;
    const auto dim = info.muted ? mutedAlpha : 1.0f;
    const auto group = info.kind == ClipKind::group;
    if (clip.tier == ClipTier::minimal)
    {
        g.setColour(clip.colour.withMultipliedAlpha(dim));
        g.fillRect(clip.bounds);
        return;
    }

    const auto body = clip.bounds.reduced(0.5f, 0.5f);
    g.setColour(clip.colour.withMultipliedAlpha(bodyAlpha * dim));
    g.fillRoundedRectangle(body, cornerRadius);
    const auto outline = timeline.resolveColour(Timeline::clipOutlineColourId);
    g.setColour(group ? clip.colour.brighter(0.6f).withMultipliedAlpha(dim) : outline);
    g.drawRoundedRectangle(body, cornerRadius, group ? 2.0f : 1.0f);

    if (clip.tier != ClipTier::full)
        return;
    const auto textColour = timeline.resolveColour(Timeline::clipTextColourId).withMultipliedAlpha(dim);
    if (group)
        drawGroupBadge(g, body, textColour);
    if (body.getHeight() < nameHeight || info.name.isEmpty())
        return;
    auto text = body.reduced(4.0f, 1.0f).withHeight(nameHeight);
    if (clip.cutLeft)
        text.setLeft(std::max(text.getX(), 4.0f));
    if (group)
        text.removeFromRight(badgeSize + 4.0f);
    g.setColour(textColour);
    g.setFont(smallFont(12.0f));
    g.drawText(info.name, text, juce::Justification::centredLeft, true);
}

void TimelineLookAndFeelMethods::drawTimelineCoverage(juce::Graphics& g, const Timeline& /*timeline*/, const juce::RectangleList<float>& spans, juce::Colour colour)
{
    g.setColour(colour.withMultipliedAlpha(0.9f));
    g.fillRectList(spans);
}

void TimelineLookAndFeelMethods::drawTimelineRuler(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> area, std::span<const RulerMark> marks)
{
    g.setColour(timeline.resolveColour(Timeline::rulerBackgroundColourId));
    g.fillRect(area);
    g.setColour(timeline.resolveColour(Timeline::separatorColourId));
    g.fillRect(area.withTop(area.getBottom() - 1.0f));

    const auto text = timeline.resolveColour(Timeline::rulerTextColourId);
    g.setFont(smallFont(11.0f));
    for (const auto& mark : marks)
    {
        const auto tickHeight = mark.major ? area.getHeight() * 0.5f : area.getHeight() * 0.2f;
        g.setColour(text.withMultipliedAlpha(mark.major ? 0.8f : 0.45f));
        g.fillRect(mark.x, area.getBottom() - tickHeight, 1.0f, tickHeight);
        if (mark.major && mark.label.isNotEmpty())
        {
            g.setColour(text);
            g.drawText(mark.label, juce::Rectangle<float>{ mark.x + 3.0f, area.getY(), 150.0f, area.getHeight() * 0.55f }, juce::Justification::bottomLeft, false);
        }
    }
}

void TimelineLookAndFeelMethods::drawTimelinePlayhead(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> area, float x, bool inRuler)
{
    g.setColour(timeline.resolveColour(Timeline::playheadColourId));
    g.fillRect(x, area.getY(), 1.0f, area.getHeight());
    if (!inRuler)
        return;
    juce::Path marker;
    marker.addTriangle(x - 5.0f, area.getBottom() - 8.0f, x + 6.0f, area.getBottom() - 8.0f, x + 0.5f, area.getBottom());
    g.fillPath(marker);
}

void TimelineLookAndFeelMethods::drawTimelineHighlight(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> range)
{
    const auto colour = timeline.resolveColour(Timeline::highlightColourId);
    g.setColour(colour.withMultipliedAlpha(0.2f));
    g.fillRect(range);
    g.setColour(colour);
    g.drawRect(range, 1.5f);
}

void TimelineLookAndFeelMethods::drawTimelineTrackHeader(juce::Graphics& g, const TrackHeader& header)
{
    const auto* timeline = header.getTimeline();
    const auto colour = [timeline, &header](int id, juce::Colour fallback) {
        if (timeline != nullptr)
            return timeline->resolveColour(id);
        const auto found = header.findColour(id, true);
        return found.isTransparent() ? fallback : found;
    };
    auto area = header.getLocalBounds().toFloat();
    g.setColour(colour(Timeline::headerBackgroundColourId, juce::Colours::darkgrey));
    g.fillRect(area);
    g.setColour(colour(Timeline::separatorColourId, juce::Colours::black));
    g.fillRect(area.withTop(area.getBottom() - 1.0f));
    g.fillRect(area.withLeft(area.getRight() - 1.0f));

    const auto& info = header.getTrackInfo();
    g.setColour(info.colour.value_or(colour(Timeline::clipColourId, juce::Colours::grey)));
    g.fillRect(area.removeFromLeft(4.0f).withTrimmedBottom(1.0f));
    if (area.getHeight() < 12.0f)
        return;
    g.setColour(colour(Timeline::headerTextColourId, juce::Colours::white));
    g.setFont(smallFont(std::min(13.0f, area.getHeight() - 4.0f)));
    g.drawText(info.name, area.reduced(6.0f, 2.0f).withHeight(std::min(area.getHeight() - 3.0f, 20.0f)), juce::Justification::centredLeft, true);
}

TimelineLookAndFeelMethods& TimelineLookAndFeelMethods::forComponent(const juce::Component& component)
{
    if (auto* methods = dynamic_cast<TimelineLookAndFeelMethods*>(&component.getLookAndFeel()))
        return *methods;
    static TimelineLookAndFeelMethods defaults;
    return defaults;
}

}

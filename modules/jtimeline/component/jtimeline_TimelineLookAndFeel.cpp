#include "jtimeline_TimelineLookAndFeel.h"

#include "jtimeline_Timeline.h"
#include "jtimeline_TrackHeader.h"

#include <jwaveform/view/jwaveform_WaveformDrawing.h>

#include <algorithm>

namespace jtimeline
{

namespace
{

constexpr float cornerRadius = 3.0f;
constexpr float bodyAlpha = 0.75f;
constexpr float mutedAlpha = 0.35f;
constexpr float nameHeight = 15.0f;

juce::Font smallFont(float height)
{
    return juce::Font{ juce::FontOptions{ height } };
}

}

TimelineMetrics TimelineLookAndFeelMethods::getTimelineMetrics(const Timeline& /*timeline*/)
{
    return {};
}

void TimelineLookAndFeelMethods::drawTimelineLane(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> lane, TrackId track, const TrackInfo& /*info*/)
{
    const auto colour = timeline.resolveColour(Timeline::laneColourId);
    g.setColour(timeline.isTrackSelected(track) ? colour.interpolatedWith(timeline.resolveColour(Timeline::selectionColourId), 0.08f) : colour);
    g.fillRect(lane);
    g.setColour(timeline.resolveColour(Timeline::separatorColourId));
    g.fillRect(lane.withTop(lane.getBottom() - 1.0f));
}

void TimelineLookAndFeelMethods::drawTimelineClip(juce::Graphics& g, const Timeline& timeline, const ClipPaint& clip)
{
    const auto& info = *clip.info;
    const auto dim = info.muted ? mutedAlpha : 1.0f;
    const auto group = info.kind == ClipKind::group;
    const auto selection = timeline.resolveColour(Timeline::selectionColourId);
    const auto fill = clip.selected ? clip.colour.interpolatedWith(selection, 0.25f) : clip.colour;
    if (clip.tier == ClipTier::minimal)
    {
        g.setColour(fill.withMultipliedAlpha(dim));
        g.fillRect(clip.bounds);
        return;
    }

    const auto body = clip.bounds.reduced(0.5f, 0.5f);
    const auto corner = group ? cornerRadius : 0.0f;
    g.setColour(fill.withMultipliedAlpha(bodyAlpha * dim));
    g.fillRoundedRectangle(body, corner);

    if (clip.waveform.status != WaveformStatus::none)
        drawTimelineClipWaveform(g, timeline, clip, juce::Rectangle<float>::leftTopRightBottom(clip.bounds.getX(), body.getY() + 1.0f, clip.bounds.getRight(), body.getBottom() - 1.0f));
    if (clip.tier == ClipTier::full && !group)
        drawTimelineClipFades(g, timeline, clip, body);

    const auto outline = timeline.resolveColour(Timeline::clipOutlineColourId);
    g.setColour(group ? clip.colour.brighter(0.6f).withMultipliedAlpha(dim) : outline);
    g.drawRoundedRectangle(body, corner, group ? 2.0f : 1.0f);
    if (clip.selected)
    {
        g.setColour(selection);
        g.drawRoundedRectangle(body.reduced(0.5f), corner, 2.0f);
    }
    if (clip.cursor && body.getWidth() > 8.0f && body.getHeight() > 8.0f)
    {
        g.setColour(timeline.resolveColour(Timeline::keyboardCursorColourId));
        g.drawRoundedRectangle(body.reduced(3.0f), corner, 1.5f);
    }

    if (clip.tier != ClipTier::full || body.getHeight() < nameHeight || info.name.isEmpty())
        return;
    auto text = body.reduced(4.0f, 1.0f).withHeight(nameHeight);
    if (clip.cutLeft)
        text.setLeft(std::max(text.getX(), 4.0f));
    g.setFont(smallFont(12.0f));
    g.setColour(juce::Colours::black.withAlpha(0.5f * dim));
    g.drawText(info.name, text.translated(0.0f, 1.0f), juce::Justification::centredLeft, true);
    g.setColour(timeline.resolveColour(Timeline::clipTextColourId).withMultipliedAlpha(dim));
    g.drawText(info.name, text, juce::Justification::centredLeft, true);
}

void TimelineLookAndFeelMethods::drawTimelineClipWaveform(juce::Graphics& g, const Timeline& timeline, const ClipPaint& clip, juce::Rectangle<float> area)
{
    const auto& waveform = clip.waveform;
    const auto dim = clip.info->muted ? mutedAlpha : 1.0f;
    const auto drawable = waveform.status == WaveformStatus::loading || waveform.status == WaveformStatus::ready || waveform.status == WaveformStatus::readError;
    if (drawable && waveform.peaks != nullptr)
    {
        jwaveform::WaveformStyle style;
        style.minLaneHeight = getTimelineMetrics(timeline).minWaveformHeight;
        style.displayScale = waveform.scale;
        style.envelope = waveform.envelope;
        style.colour = timeline.resolveColour(Timeline::waveformColourId).withMultipliedAlpha(dim);
        jwaveform::drawWaveform(g, area, *waveform.peaks, waveform.sourceStart, waveform.sourceEnd, style, waveform.samples);
    }

    const auto message = [&waveform] -> juce::String {
        switch (waveform.status)
        {
            case WaveformStatus::cannotOpen:
                return "Offline";
            case WaveformStatus::readError:
                return "Read error";
            case WaveformStatus::rateMismatch:
                return "Sample rate mismatch";
            case WaveformStatus::none:
            case WaveformStatus::loading:
            case WaveformStatus::ready:
                break;
        }
        return {};
    }();
    if (message.isEmpty() || clip.tier != ClipTier::full || area.getHeight() < nameHeight)
        return;
    auto text = area.reduced(4.0f, 1.0f).removeFromBottom(nameHeight);
    if (clip.cutLeft)
        text.setLeft(std::max(text.getX(), 4.0f));
    g.setColour(timeline.resolveColour(Timeline::clipTextColourId).withMultipliedAlpha(0.8f * dim));
    g.setFont(smallFont(11.0f).italicised());
    g.drawText(message, text, juce::Justification::centredLeft, true);
}

void TimelineLookAndFeelMethods::drawTimelineClipFades(juce::Graphics& g, const Timeline& timeline, const ClipPaint& clip, juce::Rectangle<float> body)
{
    const auto& info = *clip.info;
    const auto length = static_cast<double>(info.range().length());
    const auto pixelsPerSample = (clip.endX - clip.startX) / length;
    const auto fadeIn = std::clamp(static_cast<double>(info.fadeIn), 0.0, length) * pixelsPerSample;
    const auto fadeOut = std::clamp(static_cast<double>(info.fadeOut), 0.0, length) * pixelsPerSample;
    const auto dim = info.muted ? mutedAlpha : 1.0f;
    const auto shade = timeline.resolveColour(Timeline::fadeColourId).withMultipliedAlpha(dim);
    const auto line = timeline.resolveColour(Timeline::clipTextColourId).withMultipliedAlpha(0.7f * dim);
    const auto top = static_cast<double>(body.getY());
    const auto bottom = static_cast<double>(body.getBottom());

    const auto shadeAbove = [&](double x1, double y1, double x2, double y2) {
        const auto left = std::max(x1, static_cast<double>(body.getX()));
        const auto right = std::min(x2, static_cast<double>(body.getRight()));
        if (!(right > left) || !(x2 > x1))
            return;
        const auto yAt = [&](double x) { return static_cast<float>(y1 + ((y2 - y1) * (x - x1) / (x2 - x1))); };
        const auto l = static_cast<float>(left);
        const auto r = static_cast<float>(right);
        juce::Path region;
        region.startNewSubPath(l, static_cast<float>(top));
        region.lineTo(l, yAt(left));
        region.lineTo(r, yAt(right));
        region.lineTo(r, static_cast<float>(top));
        region.closeSubPath();
        g.setColour(shade);
        g.fillPath(region);
        g.setColour(line);
        g.drawLine(l, yAt(left), r, yAt(right), 1.0f);
    };
    if (fadeIn > 0.0)
        shadeAbove(clip.startX, bottom, clip.startX + fadeIn, top);
    if (fadeOut > 0.0)
        shadeAbove(clip.endX - fadeOut, top, clip.endX, bottom);
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

void TimelineLookAndFeelMethods::drawTimelineMarkerStrip(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> area, std::span<const MarkerPaint> markers, std::optional<juce::Rectangle<float>> timelineSelection)
{
    g.setColour(timeline.resolveColour(Timeline::rulerBackgroundColourId).darker(0.15f));
    g.fillRect(area);
    g.setColour(timeline.resolveColour(Timeline::separatorColourId));
    g.fillRect(area.withTop(area.getBottom() - 1.0f));

    g.setFont(smallFont(11.0f));
    for (std::size_t index = 0; index < markers.size(); ++index)
    {
        const auto& marker = markers[index];
        const auto nextX = index + 1 < markers.size() ? markers[index + 1].x : area.getRight() + 150.0f;
        g.setColour(marker.colour);
        g.fillRect(marker.x, area.getY() + 2.0f, 1.0f, area.getHeight() - 3.0f);
        if (nextX - marker.x < 9.0f)
            continue;
        juce::Path flag;
        flag.addTriangle(marker.x, area.getY() + 2.0f, marker.x + 7.0f, area.getY() + 6.0f, marker.x, area.getY() + 10.0f);
        g.fillPath(flag);
        if (marker.marker->name.isEmpty())
            continue;
        const auto width = std::min(nextX - marker.x - 10.0f, 150.0f);
        if (width < 12.0f)
            continue;
        g.setColour(timeline.resolveColour(Timeline::rulerTextColourId));
        g.drawText(marker.marker->name, juce::Rectangle<float>{ marker.x + 9.0f, area.getY(), width, area.getHeight() - 1.0f }, juce::Justification::centredLeft, true);
    }

    if (timelineSelection)
    {
        const auto colour = timeline.resolveColour(Timeline::timelineSelectionColourId);
        const auto bar = timelineSelection->withTrimmedTop(area.getHeight() * 0.55f).withTrimmedBottom(2.0f);
        g.setColour(colour.withMultipliedAlpha(0.75f));
        g.fillRect(bar);
        g.setColour(colour);
        g.fillRect(bar.withWidth(2.0f));
        g.fillRect(bar.withLeft(bar.getRight() - 2.0f));
    }
}

void TimelineLookAndFeelMethods::drawTimelineTimeSelection(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> area, bool inRuler)
{
    const auto colour = timeline.resolveColour(Timeline::timeSelectionColourId);
    g.setColour(colour.withMultipliedAlpha(inRuler ? 0.45f : 0.18f));
    g.fillRect(area);
    g.setColour(colour.withMultipliedAlpha(0.8f));
    g.fillRect(area.withWidth(1.0f));
    g.fillRect(area.withLeft(area.getRight() - 1.0f));
}

void TimelineLookAndFeelMethods::drawTimelineRubberBand(juce::Graphics& g, const Timeline& timeline, juce::Rectangle<float> area)
{
    const auto colour = timeline.resolveColour(Timeline::selectionColourId);
    g.setColour(colour.withMultipliedAlpha(0.12f));
    g.fillRect(area);
    g.setColour(colour.withMultipliedAlpha(0.7f));
    g.drawRect(area, 1.0f);
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
    const auto selected = timeline != nullptr && timeline->isTrackSelected(header.getTrackId());
    const auto background = colour(Timeline::headerBackgroundColourId, juce::Colours::darkgrey);
    g.setColour(selected ? background.interpolatedWith(colour(Timeline::selectionColourId, juce::Colours::white), 0.2f) : background);
    g.fillRect(area);
    const auto cursor = timeline != nullptr && timeline->hasKeyboardFocus(true) && timeline->getKeyboardCursor() == KeyboardCursor{ .track = header.getTrackId(), .clip = std::nullopt };
    if (cursor)
    {
        g.setColour(colour(Timeline::keyboardCursorColourId, juce::Colours::white));
        g.drawRect(area.reduced(2.0f).withTrimmedLeft(4.0f), 1.5f);
    }
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

#include "jtimeline_TimelineParts.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace jtimeline::detail
{

namespace
{

constexpr double clipMargin = 8.0;
constexpr double labelWidth = 160.0;

SamplePosition minGapFor(double samplesPerPixel)
{
    return std::max<SamplePosition>(1, static_cast<SamplePosition>(std::ceil(samplesPerPixel)));
}

}

//==============================================================================
Ruler::Ruler(Timeline& owner) :
    timeline(owner)
{
    setWantsKeyboardFocus(false);
}

void Ruler::paint(juce::Graphics& g)
{
    const auto& mapping = timeline.mapping;
    const auto width = static_cast<double>(getWidth());
    const auto scale = ruler::scaleFor(mapping.getSamplesPerPixel(), timeline.sampleRate);
    const auto ticks = ruler::ticksFor(mapping.scrolledBy(-labelWidth), width + labelWidth, scale);
    const auto& format = timeline.rulerFormatter ? timeline.rulerFormatter : RulerFormatter{ ruler::defaultLabel };

    std::vector<RulerMark> marks;
    marks.reserve(ticks.size());
    for (const auto& tick : ticks)
    {
        const auto x = static_cast<float>(std::clamp(mapping.toPixelAt(tick.position), -labelWidth, width + clipMargin));
        marks.push_back({ .x = x, .major = tick.major, .label = tick.major ? format(tick.position, timeline.sampleRate, scale.majorSamples) : juce::String{} });
    }

    auto& methods = TimelineLookAndFeelMethods::forComponent(timeline);
    const auto area = getLocalBounds().toFloat();
    methods.drawTimelineRuler(g, timeline, area, marks);
    const auto playheadX = mapping.toPixel(timeline.playhead);
    if (playheadX >= -clipMargin && playheadX <= width + clipMargin)
        methods.drawTimelinePlayhead(g, timeline, area, static_cast<float>(playheadX), true);
}

void Ruler::mouseDown(const juce::MouseEvent& event)
{
    timeline.grabKeyboardFocus();
    pick(event);
}

void Ruler::mouseDrag(const juce::MouseEvent& event)
{
    pick(event);
}

void Ruler::pick(const juce::MouseEvent& event)
{
    if (timeline.onTimeClicked)
        timeline.onTimeClicked(std::clamp<SamplePosition>(timeline.mapping.nearestSampleTo(static_cast<double>(event.position.x)), 0, timeline.length));
}

//==============================================================================
HeaderArea::HeaderArea(Timeline& owner) :
    timeline(owner)
{
    setWantsKeyboardFocus(false);
}

void HeaderArea::clear()
{
    bound.clear();
    pool.clear();
    removeAllChildren();
}

void HeaderArea::update()
{
    const auto span = timeline.layout.visibleTracks(timeline.trackScroll, getHeight());
    std::unordered_map<TrackId, TrackHeader*> next;
    for (auto index = span.begin; index < span.end; ++index)
    {
        const auto id = timeline.tracks[static_cast<std::size_t>(index)].id;
        if (const auto found = bound.find(id); found != bound.end())
        {
            next.emplace(id, found->second);
            bound.erase(found);
        }
    }

    std::vector<TrackHeader*> free;
    for (const auto& header : pool)
        if (std::ranges::none_of(next, [&header](const auto& entry) { return entry.second == header.get(); }))
            free.push_back(header.get());

    for (auto index = span.begin; index < span.end; ++index)
    {
        const auto& track = timeline.tracks[static_cast<std::size_t>(index)];
        auto* header = next.contains(track.id) ? next[track.id] : nullptr;
        if (header == nullptr)
        {
            if (!free.empty())
            {
                header = free.back();
                free.pop_back();
            }
            else
            {
                auto created = timeline.headerFactory ? timeline.headerFactory() : nullptr;
                if (created == nullptr)
                    created = std::make_unique<TrackHeader>();
                header = created.get();
                addChildComponent(*header);
                pool.push_back(std::move(created));
            }
            next.emplace(track.id, header);
        }
        header->bind(timeline, track.id, track.info);
        header->setBounds(0, timeline.layout.getTop(index) - timeline.trackScroll, getWidth(), timeline.layout.getHeight(index));
        header->setVisible(true);
    }
    for (auto* header : free)
        header->setVisible(false);
    bound = std::move(next);
}

//==============================================================================
ClipArea::ClipArea(Timeline& owner) :
    timeline(owner)
{
    setWantsKeyboardFocus(false);
    setOpaque(true);
}

void ClipArea::paint(juce::Graphics& g)
{
    auto& methods = TimelineLookAndFeelMethods::forComponent(timeline);
    g.fillAll(timeline.resolveColour(Timeline::backgroundColourId));
    if (timeline.tracks.empty())
        return;

    const auto mapping = timeline.overview ? timeline.overviewMapping() : timeline.mapping;
    const auto width = static_cast<double>(getWidth());
    const auto visible = mapping.visibleRange(width);
    const auto bounds = g.getClipBounds().toFloat();
    for (auto index = 0; std::cmp_less(index, timeline.tracks.size()); ++index)
    {
        const auto lane = timeline.laneBounds(index);
        if (lane.getY() >= bounds.getBottom())
            break;
        if (lane.getBottom() <= bounds.getY())
            continue;
        const auto& track = timeline.tracks[static_cast<std::size_t>(index)];
        methods.drawTimelineLane(g, timeline, lane, track.id, track.info);
        if (timeline.overview)
            paintOverviewTrack(g, methods, index, lane);
        else
            paintTrack(g, methods, index, lane, visible);
    }

    if (timeline.overview && !timeline.highlighted.isEmpty())
    {
        const auto left = mapping.toGraphicsX(timeline.highlighted.start, width);
        const auto right = mapping.toGraphicsX(timeline.highlighted.end, width);
        methods.drawTimelineHighlight(g, timeline, juce::Rectangle<float>::leftTopRightBottom(left, 0.0f, std::max(right, left + 1.0f), static_cast<float>(getHeight())));
    }

    const auto playheadX = mapping.toPixel(timeline.playhead);
    if (playheadX >= -clipMargin && playheadX <= width + clipMargin)
        methods.drawTimelinePlayhead(g, timeline, getLocalBounds().toFloat(), static_cast<float>(playheadX), false);
}

void ClipArea::paintTrack(juce::Graphics& g, TimelineLookAndFeelMethods& methods, int index, juce::Rectangle<float> lane, SampleRange visible)
{
    const auto& mapping = timeline.mapping;
    const auto& track = timeline.tracks[static_cast<std::size_t>(index)];
    const auto& clips = timeline.clipsFor(index, visible);
    const auto metrics = timeline.metrics();
    const auto body = lane.reduced(0.0f, metrics.clipInset);
    const auto width = static_cast<double>(getWidth());
    const auto bounds = g.getClipBounds().toFloat();
    const auto toStrip = [&](const SampleRange& span) {
        const auto left = mapping.toGraphicsX(span.start, width, clipMargin);
        const auto right = std::max(mapping.toGraphicsX(span.end, width, clipMargin), left + 1.0f);
        return juce::Rectangle<float>::leftTopRightBottom(left, body.getY(), right, body.getBottom());
    };

    juce::RectangleList<float> coverage;
    if (clips.dense)
    {
        for (const auto& span : clips.spans)
            if (span.intersects(visible))
                coverage.addWithoutMerging(toStrip(span));
        methods.drawTimelineCoverage(g, timeline, coverage, timeline.colourFor(track));
        return;
    }

    std::vector<SampleRange> narrow;
    for (const auto id : clips.order)
    {
        const auto* info = timeline.cachedClip(id);
        if (info != nullptr && info->range().intersects(visible) && tierFor(mapping.toPixelLength(info->range().length()), metrics.detail) == ClipTier::coverage)
            narrow.push_back(info->range());
    }
    for (const auto& span : mergeSpans(std::move(narrow), minGapFor(mapping.getSamplesPerPixel())))
        coverage.addWithoutMerging(toStrip(span));
    if (!coverage.isEmpty())
        methods.drawTimelineCoverage(g, timeline, coverage, timeline.colourFor(track));

    for (const auto id : clips.order)
    {
        const auto* info = timeline.cachedClip(id);
        if (info == nullptr || !info->range().intersects(visible))
            continue;
        const auto range = info->range();
        const auto tier = tierFor(mapping.toPixelLength(range.length()), metrics.detail);
        if (tier == ClipTier::coverage)
            continue;
        const auto left = mapping.toPixel(range.start);
        const auto right = mapping.toPixel(range.end);
        const auto x1 = static_cast<float>(std::max(left, -clipMargin));
        const auto x2 = std::max(static_cast<float>(std::min(right, width + clipMargin)), x1 + 1.0f);
        const auto clipBounds = juce::Rectangle<float>::leftTopRightBottom(x1, body.getY(), x2, body.getBottom());
        if (!clipBounds.intersects(bounds))
            continue;
        methods.drawTimelineClip(g, timeline, { .id = id, .info = info, .bounds = clipBounds, .cutLeft = left<-clipMargin, .cutRight = right> width + clipMargin, .tier = tier, .colour = timeline.colourFor(*info) });
    }
}

void ClipArea::paintOverviewTrack(juce::Graphics& g, TimelineLookAndFeelMethods& methods, int index, juce::Rectangle<float> lane)
{
    const auto mapping = timeline.overviewMapping();
    const auto& track = timeline.tracks[static_cast<std::size_t>(index)];
    if (timeline.overviewSamplesPerPixel != mapping.getSamplesPerPixel())
    {
        timeline.overviewSpans.clear();
        timeline.overviewSamplesPerPixel = mapping.getSamplesPerPixel();
    }
    auto found = timeline.overviewSpans.find(track.id);
    if (found == timeline.overviewSpans.end())
    {
        auto spans = timeline.length > 0 ? timeline.spansFor(track.id, { .start = 0, .end = timeline.length }, minGapFor(mapping.getSamplesPerPixel())) : std::vector<SampleRange>{};
        found = timeline.overviewSpans.emplace(track.id, std::move(spans)).first;
    }

    const auto width = static_cast<double>(getWidth());
    juce::RectangleList<float> coverage;
    for (const auto& span : found->second)
    {
        const auto left = mapping.toGraphicsX(span.start, width);
        const auto right = std::max(mapping.toGraphicsX(span.end, width), left + 1.0f);
        coverage.addWithoutMerging(juce::Rectangle<float>::leftTopRightBottom(left, lane.getY(), right, lane.getBottom()));
    }
    methods.drawTimelineCoverage(g, timeline, coverage, timeline.colourFor(track));
}

void ClipArea::mouseDown(const juce::MouseEvent& event)
{
    if (timeline.overview)
    {
        pickInOverview(event);
        return;
    }
    timeline.grabKeyboardFocus();
}

void ClipArea::mouseDrag(const juce::MouseEvent& event)
{
    if (timeline.overview)
        pickInOverview(event);
}

void ClipArea::pickInOverview(const juce::MouseEvent& event)
{
    if (timeline.onTimeClicked)
        timeline.onTimeClicked(std::clamp<SamplePosition>(timeline.overviewMapping().nearestSampleTo(static_cast<double>(event.position.x)), 0, timeline.length));
}

}

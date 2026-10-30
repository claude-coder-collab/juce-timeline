#include "jtimeline_TimelineParts.h"

#include "jtimeline_JuceInput.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace jtimeline::detail
{

namespace
{

constexpr double clipMargin = 8.0;
constexpr double labelWidth = 160.0;
constexpr double markerHitLeft = 9.0;
constexpr double markerHitRight = 3.0;
constexpr int dragThreshold = 3;

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
    setAccessible(false);
}

juce::Rectangle<float> Ruler::timeScaleArea() const
{
    auto area = getLocalBounds().toFloat();
    if (timeline.hasMarkerStrip())
        area.removeFromTop(static_cast<float>(timeline.metrics().markerStripHeight));
    return area;
}

void Ruler::paint(juce::Graphics& g)
{
    const auto& mapping = timeline.mapping;
    const auto width = static_cast<double>(getWidth());
    const auto scale = ruler::scaleFor(mapping.getSamplesPerPixel(), timeline.sampleRate);
    const auto ticks = ruler::ticksFor(mapping.scrolledBy(-labelWidth), width + labelWidth, scale);
    const auto& format = timeline.rulerFormatter ? timeline.rulerFormatter : RulerFormatter{ ruler::defaultLabel };
    const auto toX = [&mapping, width](SamplePosition position) { return mapping.toGraphicsX(position, width, clipMargin); };

    std::vector<RulerMark> marks;
    marks.reserve(ticks.size());
    for (const auto& tick : ticks)
    {
        const auto x = static_cast<float>(std::clamp(mapping.toPixelAt(tick.position), -labelWidth, width + clipMargin));
        marks.push_back({ .x = x, .major = tick.major, .label = tick.major ? format(tick.position, timeline.sampleRate, scale.majorSamples) : juce::String{} });
    }

    auto& methods = TimelineLookAndFeelMethods::forComponent(timeline);
    const auto area = getLocalBounds().toFloat();
    const auto timeScale = timeScaleArea();
    if (timeline.hasMarkerStrip())
    {
        const auto strip = area.withBottom(timeScale.getY());
        const auto visible = mapping.scrolledBy(-labelWidth).visibleRange(width + labelWidth + clipMargin);
        std::vector<MarkerPaint> markers;
        for (const auto& marker : timeline.markersFor(visible))
            if (marker.position >= visible.start && marker.position < visible.end)
                markers.push_back({ .x = static_cast<float>(std::clamp(mapping.toPixel(marker.position), -labelWidth, width + clipMargin)), .marker = &marker, .colour = marker.colour.value_or(timeline.resolveColour(Timeline::markerColourId)) });
        std::optional<juce::Rectangle<float>> selectionArea;
        if (const auto& range = timeline.timelineSelection)
            selectionArea = juce::Rectangle<float>::leftTopRightBottom(toX(range->start), strip.getY(), std::max(toX(range->end), toX(range->start) + 1.0f), strip.getBottom());
        methods.drawTimelineMarkerStrip(g, timeline, strip, markers, selectionArea);
    }
    methods.drawTimelineRuler(g, timeline, timeScale, marks);
    if (const auto& range = timeline.selection.getTimeRange())
        methods.drawTimelineTimeSelection(g, timeline, juce::Rectangle<float>::leftTopRightBottom(toX(range->start), timeScale.getY(), std::max(toX(range->end), toX(range->start) + 1.0f), timeScale.getBottom()), true);
    const auto playheadX = mapping.toPixel(timeline.playhead);
    if (playheadX >= -clipMargin && playheadX <= width + clipMargin)
        methods.drawTimelinePlayhead(g, timeline, area, static_cast<float>(playheadX), true);
}

SamplePosition Ruler::positionAt(float x) const
{
    return std::clamp<SamplePosition>(timeline.mapping.nearestSampleTo(static_cast<double>(x)), 0, timeline.length);
}

std::optional<SamplePosition> Ruler::markerAt(juce::Point<float> position)
{
    if (!timeline.hasMarkerStrip() || position.y >= timeScaleArea().getY())
        return std::nullopt;
    const auto& mapping = timeline.mapping;
    const auto x = static_cast<double>(position.x);
    const SampleRange around{ .start = mapping.sampleAt(x - markerHitLeft), .end = mapping.sampleAt(x + markerHitRight) + 1 };
    std::optional<SamplePosition> best;
    auto bestDistance = 0.0;
    for (const auto& marker : timeline.markersFor(timeline.getVisibleRange()))
    {
        const auto offset = x - mapping.toPixel(marker.position);
        if (marker.position < around.start || marker.position >= around.end || offset < -markerHitRight || offset > markerHitLeft)
            continue;
        if (!best || std::abs(offset) < bestDistance)
        {
            best = marker.position;
            bestDistance = std::abs(offset);
        }
    }
    return best;
}

void Ruler::mouseDown(const juce::MouseEvent& event)
{
    timeline.focusFromClick();
    selecting = false;
    dragAnchor.reset();
    if (const auto marker = markerAt(event.position))
    {
        if (timeline.onTimeClicked)
            timeline.onTimeClicked(*marker);
        return;
    }
    dragAnchor = positionAt(event.position.x);
    timeline.setTimeSelection(std::nullopt);
    if (timeline.onTimeClicked)
        timeline.onTimeClicked(*dragAnchor);
}

void Ruler::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragAnchor)
        return;
    if (!selecting && event.getDistanceFromDragStart() < dragThreshold)
        return;
    selecting = true;
    const auto position = positionAt(event.position.x);
    timeline.setTimeSelection(SampleRange{ .start = std::min(*dragAnchor, position), .end = std::max(*dragAnchor, position) });
}

void Ruler::mouseUp(const juce::MouseEvent& /*event*/)
{
    if (selecting)
        if (const auto& range = timeline.selection.getTimeRange())
            Timeline::announce("Time selected, " + ruler::formatTime(static_cast<double>(range->start) / timeline.sampleRate, 0.001) + " to " + ruler::formatTime(static_cast<double>(range->end) / timeline.sampleRate, 0.001));
    selecting = false;
    dragAnchor.reset();
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

void HeaderArea::repaintHeaders()
{
    for (const auto& [id, header] : bound)
        header->repaint();
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
ClipProxy::ClipProxy(Timeline& owner) :
    timeline(owner)
{
    setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(false);
}

void ClipProxy::bind(ClipId id, const ClipInfo& info)
{
    clip = id;
    const auto title = info.name.isNotEmpty() ? info.name : juce::String{ "Untitled clip" };
    const auto description = timeline.describeClip(info);
    if (getTitle() != title)
        setTitle(title);
    if (getDescription() != description)
        setDescription(description);
}

std::unique_ptr<juce::AccessibilityHandler> ClipProxy::createAccessibilityHandler()
{
    return std::make_unique<SelectableHandler>(*this, juce::AccessibilityRole::listItem, [this] { return timeline.isClipSelected(clip); }, [this](SelectMode mode) { timeline.selectClip(clip, mode); });
}

//==============================================================================
ClipArea::ClipArea(Timeline& owner) :
    timeline(owner)
{
    setWantsKeyboardFocus(false);
    setOpaque(true);
    setTitle("Clips");
}

ClipArea::~ClipArea()
{
    cancelPendingUpdate();
}

std::unique_ptr<juce::AccessibilityHandler> ClipArea::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::list);
}

void ClipArea::paint(juce::Graphics& g)
{
    auto& methods = TimelineLookAndFeelMethods::forComponent(timeline);
    const auto wholeArea = g.getClipBounds().contains(getLocalBounds());
    paintContents(g, methods);
    if (wholeArea)
        timeline.releaseUnusedWaveforms();
}

void ClipArea::proxiesNeedUpdate()
{
    triggerAsyncUpdate();
}

void ClipArea::handleAsyncUpdate()
{
    updateProxies();
}

void ClipArea::updateProxies()
{
    cancelPendingUpdate();
    std::size_t used = 0;
    if (!timeline.overview && !timeline.tracks.empty() && getWidth() > 0)
    {
        const auto& mapping = timeline.mapping;
        const auto width = static_cast<double>(getWidth());
        const auto visible = mapping.visibleRange(width);
        const auto metrics = timeline.metrics();
        const auto span = timeline.layout.visibleTracks(timeline.trackScroll, getHeight());
        for (auto index = span.begin; index < span.end && used < maxProxies; ++index)
        {
            const auto& clips = timeline.clipsFor(index, visible);
            if (clips.dense)
                continue;
            const auto lane = timeline.laneBounds(index).reduced(0.0f, metrics.clipInset);
            for (const auto id : clips.order)
            {
                const auto* info = timeline.cachedClip(id);
                if (info == nullptr || !info->range().intersects(visible))
                    continue;
                const auto range = info->range();
                const auto tier = tierFor(mapping.toPixelLength(range.length()), metrics.detail);
                if (tier != ClipTier::full && tier != ClipTier::reduced)
                    continue;
                const auto left = std::max(mapping.toPixel(range.start), 0.0);
                const auto right = std::min(mapping.toPixel(range.end), width);
                const auto bounds = juce::Rectangle<float>::leftTopRightBottom(static_cast<float>(left), lane.getY(), static_cast<float>(right), lane.getBottom()).getSmallestIntegerContainer().getIntersection(getLocalBounds());
                if (bounds.isEmpty())
                    continue;
                if (used == proxies.size())
                {
                    proxies.push_back(std::make_unique<ClipProxy>(timeline));
                    addChildComponent(*proxies.back());
                }
                auto& proxy = *proxies[used++];
                proxy.bind(id, *info);
                if (proxy.getBounds() != bounds)
                    proxy.setBounds(bounds);
                proxy.setVisible(true);
                if (used == maxProxies)
                    break;
            }
        }
    }
    for (auto index = used; index < proxies.size(); ++index)
        proxies[index]->setVisible(false);
}

std::vector<ClipProxy*> ClipArea::getVisibleProxies() const
{
    std::vector<ClipProxy*> visible;
    for (const auto& proxy : proxies)
        if (proxy->isVisible())
            visible.push_back(proxy.get());
    return visible;
}

void ClipArea::paintContents(juce::Graphics& g, TimelineLookAndFeelMethods& methods)
{
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

    if (const auto& range = timeline.selection.getTimeRange(); range && !timeline.overview)
    {
        const auto left = mapping.toGraphicsX(range->start, width, clipMargin);
        const auto right = std::max(mapping.toGraphicsX(range->end, width, clipMargin), left + 1.0f);
        const auto bottom = std::min(static_cast<float>(getHeight()), static_cast<float>(timeline.layout.getTotalHeight() - timeline.trackScroll));
        if (bottom > 0.0f)
            methods.drawTimelineTimeSelection(g, timeline, juce::Rectangle<float>::leftTopRightBottom(left, 0.0f, right, bottom), false);
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

    if (banding)
        methods.drawTimelineRubberBand(g, timeline, band);
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
    const auto focused = timeline.hasKeyboardFocus(true);
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
        const auto x1 = std::max(left, -clipMargin);
        const auto x2 = std::max(std::min(right, width + clipMargin), x1 + 1.0);
        const auto clipBounds = juce::Rectangle<float>::leftTopRightBottom(static_cast<float>(x1), body.getY(), static_cast<float>(x2), body.getBottom());
        if (!clipBounds.intersects(bounds))
            continue;
        ClipWaveform waveform;
        if (tier != ClipTier::minimal && body.getHeight() >= metrics.minWaveformHeight && info->source.has_value() && hasWaveform(*info))
        {
            waveform = timeline.waveformFor(*info, *info->source);
            const auto samplesPerPixel = mapping.getSamplesPerPixel();
            waveform.sourceStart = static_cast<double>(info->sourceOffset) + ((x1 - left) * samplesPerPixel);
            waveform.sourceEnd = static_cast<double>(info->sourceOffset) + ((x2 - left) * samplesPerPixel);
        }
        methods.drawTimelineClip(g, timeline, { .id = id, .info = info, .bounds = clipBounds, .cutLeft = left<-clipMargin, .cutRight = right> width + clipMargin, .tier = tier, .colour = timeline.colourFor(*info), .startX = left, .endX = right, .waveform = std::move(waveform), .selected = timeline.selection.contains(id), .cursor = focused && timeline.keyboardCursor.clip == id });
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
    timeline.focusFromClick();
    const auto mode = selectModeFrom(event.mods);
    const auto shift = event.mods.isShiftDown();
    const auto clip = timeline.clipAt(event.getEventRelativeTo(&timeline).position);
    shiftClicked.reset();
    if (clip && !shift)
    {
        timeline.selectClip(*clip, mode);
        return;
    }
    shiftClicked = clip;
    bandStart = event.position;
    banding = false;
    band = {};
    if (mode == SelectMode::replace && !shift)
    {
        bandBase.clear();
        if (timeline.selection.clearObjects())
            timeline.selectionChanged();
    }
    else
    {
        bandBase.assign(timeline.selection.getClips().begin(), timeline.selection.getClips().end());
    }
}

void ClipArea::mouseDrag(const juce::MouseEvent& event)
{
    if (timeline.overview)
    {
        pickInOverview(event);
        return;
    }
    if (!bandStart || (!banding && event.getDistanceFromDragStart() < dragThreshold))
        return;
    banding = true;
    updateBand(juce::Rectangle<float>{ *bandStart, event.position }.getIntersection(getLocalBounds().toFloat()));
    auto ids = bandBase;
    const auto inside = timeline.clipsInArea(band);
    ids.insert(ids.end(), inside.begin(), inside.end());
    timeline.selectClipsByGesture(ids);
}

void ClipArea::mouseUp(const juce::MouseEvent& /*event*/)
{
    if (!banding && shiftClicked)
        timeline.extendClipSelection(*shiftClicked);
    if (banding || shiftClicked)
        Timeline::announce(juce::String{ static_cast<juce::int64>(timeline.selection.getClips().size()) } + " clips selected");
    shiftClicked.reset();
    bandStart.reset();
    banding = false;
    updateBand({});
    bandBase.clear();
}

void ClipArea::updateBand(juce::Rectangle<float> next)
{
    repaint(band.expanded(2.0f).getSmallestIntegerContainer());
    band = next;
    repaint(band.expanded(2.0f).getSmallestIntegerContainer());
}

void ClipArea::pickInOverview(const juce::MouseEvent& event)
{
    if (timeline.onTimeClicked)
        timeline.onTimeClicked(std::clamp<SamplePosition>(timeline.overviewMapping().nearestSampleTo(static_cast<double>(event.position.x)), 0, timeline.length));
}

}

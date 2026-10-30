#include "jtimeline_Timeline.h"

#include "jtimeline_JuceInput.h"
#include "jtimeline_TimelineParts.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <unordered_set>
#include <utility>

namespace jtimeline
{

namespace
{

constexpr double minWaveformScale = 0.25;
constexpr double maxWaveformScale = 64.0;
constexpr double overscrollFraction = 0.1;
constexpr double clipMargin = 8.0;
constexpr int playheadRepaintHalfWidth = 6;
constexpr double densePixelsPerClip = 2.0;
constexpr int waveformRepaintIntervalMs = 30;
constexpr double revealMarginFraction = 0.05;

double sanitisedRate(double rate)
{
    return std::isfinite(rate) && rate > 0.0 ? rate : 48000.0;
}

ClipInfo normalised(ClipInfo info)
{
    info.length = std::max<SamplePosition>(info.length, 1);
    return info;
}

}

Timeline::Timeline() :
    ruler(std::make_unique<detail::Ruler>(*this)),
    headers(std::make_unique<detail::HeaderArea>(*this)),
    clipArea(std::make_unique<detail::ClipArea>(*this))
{
    setWantsKeyboardFocus(true);
    setTitle("Timeline");
    for (auto* child : std::initializer_list<juce::Component*>{ ruler.get(), headers.get(), clipArea.get(), &horizontalBar, &verticalBar })
        addAndMakeVisible(child);
    horizontalBar.setAutoHide(false);
    verticalBar.setAutoHide(false);
    horizontalBar.addListener(this);
    verticalBar.addListener(this);
}

Timeline::~Timeline()
{
    stopTimer();
    waveforms.clear();
    if (notifier != nullptr)
        notifier->removeListener(this);
    headers->clear();
}

//==============================================================================
void Timeline::setModel(TimelineModel newModel)
{
    if (notifier != nullptr)
        notifier->removeListener(this);
    model = std::move(newModel);
    notifier = model.isComplete() ? model.notifier : nullptr;
    if (notifier != nullptr)
        notifier->addListener(this);
    refresh();
    if (clipAreaWidth() > 0.0)
        zoomToFit();
    else
        fitted = false;
}

void Timeline::refresh()
{
    readTimeline();
    readTracks();
    readMarkers();
    trackClips.clear();
    clipInfos.clear();
    overviewSpans.clear();
    if (!overview && ruler->getHeight() != rulerTotalHeight() && !getLocalBounds().isEmpty())
        resized();
    layoutChanged();
    setMapping(mapping);
    validateSelection(std::nullopt);
    clipArea->proxiesNeedUpdate();
    repaint();
}

void Timeline::setPeakCache(std::shared_ptr<jwaveform::PeakCache> cache)
{
    peakCache = std::move(cache);
    waveforms.clear();
    changedWaveforms.clear();
    stopTimer();
    setMapping(mapping);
    clipArea->repaint();
}

void Timeline::readTimeline()
{
    const auto complete = model.isComplete();
    sampleRate = sanitisedRate(complete ? model.sampleRate() : 0.0);
    length = complete ? std::max<SamplePosition>(model.length(), 0) : 0;
}

void Timeline::readTracks()
{
    tracks.clear();
    trackIndices.clear();
    if (!model.isComplete())
        return;
    for (const auto id : model.trackIds())
    {
        if (trackIndices.contains(id))
            continue;
        if (auto info = model.trackInfo(id))
        {
            trackIndices.emplace(id, static_cast<int>(tracks.size()));
            tracks.push_back({ .id = id, .info = std::move(*info) });
        }
    }
}

void Timeline::modelChanged(const Change& change)
{
    std::visit(
        [this]<typename Kind>(const Kind& value) {
            if constexpr (std::is_same_v<Kind, TracksChanged>)
            {
                readTracks();
                trackClips.clear();
                overviewSpans.clear();
                layoutChanged();
                validateSelection(std::span<const ClipId>{});
                repaint();
            }
            else if constexpr (std::is_same_v<Kind, ClipsChanged>)
            {
                clipsChanged(value.ids);
            }
            else if constexpr (std::is_same_v<Kind, MarkersChanged>)
            {
                readMarkers();
                ruler->repaint();
            }
            else
            {
                refresh();
            }
        },
        change
    );
}

void Timeline::notifierDestroyed(TimelineNotifier& destroyedNotifier)
{
    if (&destroyedNotifier != notifier)
        return;
    notifier = nullptr;
    model = {};
    refresh();
}

void Timeline::clipsChanged(std::span<const ClipId> ids)
{
    validateSelection(ids);
    clipArea->proxiesNeedUpdate();
    if (overview)
    {
        overviewSpans.clear();
        clipArea->repaint();
        return;
    }

    std::unordered_set<TrackId> touched;
    for (const auto id : ids)
    {
        if (const auto found = clipInfos.find(id); found != clipInfos.end())
        {
            repaintClip(found->second);
            touched.insert(found->second.track);
            clipInfos.erase(found);
        }
        auto info = model.clipInfo ? model.clipInfo(id) : std::nullopt;
        if (!info)
            continue;
        const auto clip = normalised(std::move(*info));
        touched.insert(clip.track);
        if (const auto cached = trackClips.find(clip.track); cached != trackClips.end() && !cached->second.dense)
        {
            repaintClip(clip);
            clipInfos.insert_or_assign(id, clip);
        }
    }

    for (const auto track : touched)
    {
        const auto cached = trackClips.find(track);
        if (cached == trackClips.end())
            continue;
        if (cached->second.dense)
            if (const auto index = trackIndex(track); index >= 0)
                clipArea->repaint(laneBounds(index).getSmallestIntegerContainer());
        trackClips.erase(cached);
    }
}

//==============================================================================
TimelineMetrics Timeline::metrics() const
{
    return TimelineLookAndFeelMethods::forComponent(*this).getTimelineMetrics(*this);
}

double Timeline::clipAreaWidth() const
{
    return static_cast<double>(clipArea->getWidth());
}

int Timeline::clipAreaHeight() const
{
    return clipArea->getHeight();
}

double Timeline::timelineExtent() const
{
    return std::max(static_cast<double>(length), ZoomLimits::minimumTimelineSeconds * sampleRate);
}

juce::Rectangle<int> Timeline::getClipAreaBounds() const
{
    return clipArea->getBounds();
}

void Timeline::resized()
{
    const auto m = metrics();
    ruler->setVisible(!overview);
    headers->setVisible(!overview);
    horizontalBar.setVisible(!overview);
    verticalBar.setVisible(!overview);
    if (overview)
    {
        clipArea->setBounds(getLocalBounds());
    }
    else
    {
        auto area = getLocalBounds();
        const auto bottom = area.removeFromBottom(m.scrollBarThickness);
        horizontalBar.setBounds(bottom.withTrimmedLeft(m.headerWidth).withTrimmedRight(m.scrollBarThickness));
        const auto rulerHeight = rulerTotalHeight();
        verticalBar.setBounds(area.removeFromRight(m.scrollBarThickness).withTrimmedTop(rulerHeight));
        ruler->setBounds(area.removeFromTop(rulerHeight).withTrimmedLeft(m.headerWidth));
        headers->setBounds(area.removeFromLeft(m.headerWidth));
        clipArea->setBounds(area);
    }

    if (!fitted && clipAreaWidth() > 0.0)
    {
        fitted = true;
        zoomToFit();
    }
    layoutChanged();
    setMapping(mapping);
    updateScrollBars();
}

void Timeline::paint(juce::Graphics& g)
{
    g.fillAll(resolveColour(backgroundColourId));
    if (overview)
        return;
    const auto m = metrics();
    const auto rulerHeight = rulerTotalHeight();
    g.setColour(resolveColour(rulerBackgroundColourId));
    g.fillRect(0, 0, m.headerWidth, rulerHeight);
    g.setColour(resolveColour(headerBackgroundColourId));
    g.fillRect(0, getHeight() - m.scrollBarThickness, m.headerWidth, m.scrollBarThickness);
    g.setColour(resolveColour(separatorColourId));
    g.fillRect(0, rulerHeight - 1, m.headerWidth, 1);
}

void Timeline::lookAndFeelChanged()
{
    resized();
    repaint();
}

void Timeline::colourChanged()
{
    repaint();
}

std::optional<juce::Colour> Timeline::specifiedColour(int colourId) const
{
    for (const auto* component = static_cast<const juce::Component*>(this); component != nullptr; component = component->getParentComponent())
        if (component->isColourSpecified(colourId))
            return component->findColour(colourId);
    if (getLookAndFeel().isColourSpecified(colourId))
        return getLookAndFeel().findColour(colourId);
    return std::nullopt;
}

juce::Colour Timeline::resolveColour(int colourId) const
{
    if (const auto specified = specifiedColour(colourId))
        return *specified;

    const auto background = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    const auto text = getLookAndFeel().findColour(juce::Label::textColourId);
    switch (colourId)
    {
        case backgroundColourId:
            return background;
        case laneColourId:
            return background.interpolatedWith(text, 0.04f);
        case separatorColourId:
            return background.interpolatedWith(text, 0.15f);
        case headerBackgroundColourId:
        case rulerBackgroundColourId:
            return background.interpolatedWith(text, 0.08f);
        case rulerTextColourId:
            return text.withMultipliedAlpha(0.8f);
        case clipColourId:
        case highlightColourId:
            return getLookAndFeel().findColour(juce::Slider::thumbColourId);
        case clipOutlineColourId:
            return juce::Colours::black.withAlpha(0.5f);
        case waveformColourId:
            return specifiedColour(clipTextColourId).value_or(text).withMultipliedAlpha(0.85f);
        case fadeColourId:
            return juce::Colours::black.withAlpha(0.3f);
        case timeSelectionColourId:
            return getLookAndFeel().findColour(juce::Slider::thumbColourId);
        case markerColourId:
            return juce::Colour{ 0xffe8a33d };
        case loopColourId:
            return juce::Colour{ 0xff5cb85c };
        case keyboardCursorColourId:
        case playheadColourId:
            return getLookAndFeel().findColour(juce::TextEditor::focusedOutlineColourId).withAlpha(1.0f);
        case clipTextColourId:
        case headerTextColourId:
        case selectionColourId:
        default:
            return text;
    }
}

juce::Colour Timeline::colourFor(const ClipInfo& info) const
{
    if (info.colour)
        return *info.colour;
    if (const auto index = trackIndex(info.track); index >= 0)
        return colourFor(tracks[static_cast<std::size_t>(index)]);
    return resolveColour(clipColourId);
}

juce::Colour Timeline::colourFor(const Track& track) const
{
    return track.info.colour.value_or(resolveColour(clipColourId));
}

//==============================================================================
ZoomLimits Timeline::getZoomLimits() const
{
    const auto samplesPerPeak = peakCache != nullptr ? peakCache->getSamplesPerPeak() : jwaveform::defaultSamplesPerPeak;
    return ZoomLimits::forTimeline(sampleRate, length, clipAreaWidth(), static_cast<double>(samplesPerPeak) / physicalPixelScale());
}

double Timeline::physicalPixelScale() const
{
    auto scale = static_cast<double>(juce::Component::getApproximateScaleFactorForComponent(this));
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()))
        scale *= display->scale;
    return std::isfinite(scale) && scale > 0.0 ? scale : 1.0;
}

TimeMapping Timeline::clamped(const TimeMapping& candidate) const
{
    const auto spp = getZoomLimits().clamp(candidate.getSamplesPerPixel());
    auto result = TimeMapping{ spp, candidate.getStartSample(), candidate.getStartFraction() };
    const auto [lowest, highest] = scrollLimits(spp);
    const auto start = result.getStartPosition();
    if (start < lowest)
        result = result.withStartPosition(lowest);
    else if (start > highest)
        result = result.withStartPosition(highest);
    return result;
}

std::pair<double, double> Timeline::scrollLimits(double samplesPerPixel) const
{
    const auto span = clipAreaWidth() * samplesPerPixel;
    const auto overscroll = span * overscrollFraction;
    return { -overscroll, std::max(0.0, timelineExtent() - span + overscroll) };
}

TimeMapping Timeline::overviewMapping() const
{
    return TimeMapping{ timelineExtent() / std::max(clipAreaWidth(), 1.0), 0 };
}

void Timeline::setMapping(const TimeMapping& newMapping)
{
    const auto next = clamped(newMapping);
    if (next == mapping)
        return;
    mapping = next;
    viewChanged();
}

void Timeline::viewChanged()
{
    updateScrollBars();
    clipArea->repaint();
    clipArea->proxiesNeedUpdate();
    ruler->repaint();
    if (onViewChanged)
        onViewChanged();
}

SampleRange Timeline::getVisibleRange() const
{
    return mapping.visibleRange(clipAreaWidth());
}

void Timeline::setVisibleRange(SampleRange range)
{
    if (range.isEmpty() || clipAreaWidth() <= 0.0)
        return;
    setMapping(TimeMapping{ getZoomLimits().clamp(static_cast<double>(range.length()) / clipAreaWidth()), range.start });
}

void Timeline::zoomToFit()
{
    setMapping(TimeMapping{ getZoomLimits().maxSamplesPerPixel, 0 });
}

void Timeline::zoomAtEditCursor(double steps, double factorPerStep)
{
    const auto target = getZoomLimits().clamp(wheel::zoomedSamplesPerPixel(mapping.getSamplesPerPixel(), steps, factorPerStep));
    setMapping(mapping.zoomedAroundSample(target, getEditCursor(), clipAreaWidth()));
}

void Timeline::scrollTo(SamplePosition position)
{
    setMapping(mapping.withSampleAt(position, 0.0));
}

void Timeline::setTrackScroll(int pixels)
{
    const auto next = std::clamp(pixels, 0, std::max(0, layout.getTotalHeight() - clipAreaHeight()));
    if (next == trackScroll)
        return;
    trackScroll = next;
    headers->update();
    clipArea->repaint();
    clipArea->proxiesNeedUpdate();
    updateScrollBars();
    if (onViewChanged)
        onViewChanged();
}

void Timeline::updateScrollBars()
{
    const auto span = clipAreaWidth() * mapping.getSamplesPerPixel();
    const auto [lowest, highest] = scrollLimits(mapping.getSamplesPerPixel());
    horizontalBar.setRangeLimits(lowest, highest + span, juce::dontSendNotification);
    horizontalBar.setCurrentRange(mapping.getStartPosition(), span, juce::dontSendNotification);
    horizontalBar.setSingleStepSize(wheel::pixelsPerNotch * mapping.getSamplesPerPixel());

    const auto visibleHeight = clipAreaHeight();
    verticalBar.setRangeLimits(0.0, static_cast<double>(std::max(layout.getTotalHeight(), visibleHeight)), juce::dontSendNotification);
    verticalBar.setCurrentRange(static_cast<double>(trackScroll), static_cast<double>(visibleHeight), juce::dontSendNotification);
    verticalBar.setSingleStepSize(wheel::pixelsPerNotch);
}

void Timeline::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &horizontalBar)
        setMapping(mapping.withStartPosition(newRangeStart));
    else
        setTrackScroll(juce::roundToInt(newRangeStart));
}

//==============================================================================
std::vector<TrackId> Timeline::getTrackIds() const
{
    std::vector<TrackId> ids;
    ids.reserve(tracks.size());
    for (const auto& track : tracks)
        ids.push_back(track.id);
    return ids;
}

int Timeline::trackIndex(TrackId track) const
{
    const auto found = trackIndices.find(track);
    return found != trackIndices.end() ? found->second : -1;
}

int Timeline::getTrackHeight(TrackId track) const
{
    if (const auto found = setHeights.find(track); found != setHeights.end())
        return found->second;
    if (const auto index = trackIndex(track); index >= 0)
        if (const auto hint = tracks[static_cast<std::size_t>(index)].info.heightHint)
            return std::max(*hint, 1);
    return defaultTrackHeight > 0 ? defaultTrackHeight : metrics().defaultTrackHeight;
}

void Timeline::setTrackHeight(TrackId track, int height)
{
    setHeights.insert_or_assign(track, std::max(height, 1));
    layoutChanged();
}

void Timeline::setDefaultTrackHeight(int height)
{
    defaultTrackHeight = std::max(height, 1);
    setHeights.clear();
    layoutChanged();
}

void Timeline::stepTrackHeights(std::span<const TrackId> tracksToStep, int direction)
{
    const auto steps = metrics().trackHeightSteps;
    for (const auto id : tracksToStep)
        setHeights.insert_or_assign(id, std::max(trackHeights::stepped(getTrackHeight(id), steps, direction), 1));
    layoutChanged();
}

void Timeline::layoutChanged()
{
    std::vector<int> heights;
    heights.reserve(tracks.size());
    for (const auto& track : tracks)
        heights.push_back(getTrackHeight(track.id));
    layout = TrackLayout{ heights };
    const auto previous = trackScroll;
    trackScroll = std::clamp(trackScroll, 0, std::max(0, layout.getTotalHeight() - clipAreaHeight()));
    headers->update();
    clipArea->repaint();
    clipArea->proxiesNeedUpdate();
    updateScrollBars();
    if (previous != trackScroll && onViewChanged)
        onViewChanged();
}

juce::Rectangle<float> Timeline::laneBounds(int index) const
{
    const auto width = static_cast<float>(clipArea->getWidth());
    if (overview)
    {
        const auto laneHeight = static_cast<float>(clipAreaHeight()) / static_cast<float>(std::max<std::size_t>(tracks.size(), 1));
        return { 0.0f, laneHeight * static_cast<float>(index), width, laneHeight };
    }
    return { 0.0f, static_cast<float>(layout.getTop(index) - trackScroll), width, static_cast<float>(layout.getHeight(index)) };
}

std::optional<TrackId> Timeline::trackAt(float y) const
{
    const auto local = y - static_cast<float>(clipArea->getY());
    if (tracks.empty() || local < 0.0f)
        return std::nullopt;
    if (overview)
    {
        const auto index = static_cast<int>(local / laneBounds(0).getHeight());
        return std::cmp_less(index, tracks.size()) ? std::optional{ tracks[static_cast<std::size_t>(index)].id } : std::nullopt;
    }
    const auto index = layout.indexAt(static_cast<int>(std::floor(local)) + trackScroll);
    return index >= 0 ? std::optional{ tracks[static_cast<std::size_t>(index)].id } : std::nullopt;
}

std::optional<ClipId> Timeline::clipAt(juce::Point<float> position) const
{
    if (overview || !model.isComplete() || !clipArea->getBounds().toFloat().contains(position))
        return std::nullopt;
    const auto track = trackAt(position.y);
    if (!track)
        return std::nullopt;
    const auto x = static_cast<double>(position.x) - static_cast<double>(clipArea->getX());
    const auto from = std::max<SamplePosition>(mapping.sampleAt(x), 0);
    const auto to = std::min(std::max(mapping.sampleAt(x + 1.0), from + 1), length);
    if (from >= to)
        return std::nullopt;
    const auto ids = model.clipsInRange(*track, from, to);
    for (const auto id : std::views::reverse(ids))
        if (model.clipInfo(id))
            return id;
    return std::nullopt;
}

//==============================================================================
const ClipInfo* Timeline::cachedClip(ClipId id) const
{
    const auto found = clipInfos.find(id);
    return found != clipInfos.end() ? &found->second : nullptr;
}

std::vector<SampleRange> Timeline::spansFor(TrackId track, SampleRange range, SamplePosition minGap) const
{
    if (model.clipSpans)
        return model.clipSpans(track, range.start, range.end, minGap);
    std::vector<SampleRange> spans;
    for (const auto id : model.clipsInRange(track, range.start, range.end))
        if (const auto info = model.clipInfo(id))
            spans.push_back(normalised(*info).range().intersection(range));
    return mergeSpans(std::move(spans), minGap);
}

const Timeline::TrackClips& Timeline::clipsFor(int index, SampleRange visible)
{
    const auto id = tracks[static_cast<std::size_t>(index)].id;
    const SampleRange timeline{ .start = 0, .end = length };
    const auto needed = visible.intersection(timeline);
    auto& entry = trackClips[id];
    const auto spp = mapping.getSamplesPerPixel();
    const auto covered = needed.isEmpty() || (entry.queried.start <= needed.start && entry.queried.end >= needed.end);
    if (entry.samplesPerPixel == spp && covered)
        return entry;

    const auto margin = visible.length() / 2;
    const SampleRange query{ .start = visible.start - margin, .end = visible.end + margin };
    entry = { .queried = query.intersection(timeline), .samplesPerPixel = spp, .dense = false, .order = {}, .spans = {} };
    if (entry.queried.isEmpty())
        return entry;

    entry.order = model.clipsInRange(id, entry.queried.start, entry.queried.end);
    const auto queriedPixels = static_cast<double>(entry.queried.length()) / spp;
    entry.dense = static_cast<double>(entry.order.size()) * densePixelsPerClip > std::max(queriedPixels, 1.0);
    if (entry.dense)
    {
        entry.spans = spansFor(id, entry.queried, std::max<SamplePosition>(1, static_cast<SamplePosition>(std::ceil(spp))));
        return entry;
    }
    for (const auto clip : entry.order)
        if (!clipInfos.contains(clip))
            if (auto info = model.clipInfo(clip))
                clipInfos.emplace(clip, normalised(std::move(*info)));

    std::size_t wanted = 0;
    for (const auto& [track, clips] : trackClips)
        wanted += clips.dense ? 0 : clips.order.size();
    if (clipInfos.size() > (2 * wanted) + 1024)
    {
        std::unordered_set<ClipId> keep;
        for (const auto& [track, clips] : trackClips)
            if (!clips.dense)
                keep.insert(clips.order.begin(), clips.order.end());
        std::erase_if(clipInfos, [&keep](const auto& item) { return !keep.contains(item.first); });
    }
    return entry;
}

juce::Rectangle<int> Timeline::clipRect(const ClipInfo& info) const
{
    const auto index = trackIndex(info.track);
    if (index < 0)
        return {};
    const auto width = clipAreaWidth();
    const auto lane = laneBounds(index);
    const auto left = mapping.toGraphicsX(info.start, width, clipMargin);
    const auto right = mapping.toGraphicsX(info.range().end, width, clipMargin);
    return juce::Rectangle<float>::leftTopRightBottom(left - 2.0f, lane.getY(), right + 2.0f, lane.getBottom()).getSmallestIntegerContainer().getIntersection(clipArea->getLocalBounds());
}

void Timeline::repaintClip(const ClipInfo& info)
{
    if (const auto area = clipRect(info); !area.isEmpty())
        clipArea->repaint(area);
}

//==============================================================================
ClipWaveform Timeline::waveformFor(const ClipInfo& info, const jwaveform::WaveformSource& source)
{
    std::shared_ptr<const jwaveform::PeakData> peaks;
    auto state = jwaveform::PeakState::none;
    if (source.peaks != nullptr)
    {
        peaks = source.peaks;
        state = peaks->isComplete() ? jwaveform::PeakState::ready : jwaveform::PeakState::loading;
    }
    else if (peakCache != nullptr)
    {
        auto found = waveforms.find(source.key);
        if (found == waveforms.end())
            found = waveforms.emplace(source.key, Waveform{ .handle = peakCache->request(source, [this, key = source.key] { waveformChanged(key); }), .used = false }).first;
        found->second.used = true;
        peaks = found->second.handle.getData();
        state = found->second.handle.getState();
    }
    const auto status = waveformStatusFor(state, peaks.get(), sampleRate);
    return { .peaks = status == WaveformStatus::rateMismatch ? nullptr : std::move(peaks), .status = status, .sourceStart = 0.0, .sourceEnd = 0.0, .scale = getWaveformScaleFor(info.track), .envelope = envelopeFor(info) };
}

void Timeline::waveformChanged(const juce::String& key)
{
    changedWaveforms.insert(key);
    if (!isTimerRunning())
        startTimer(waveformRepaintIntervalMs);
}

void Timeline::timerCallback()
{
    stopTimer();
    if (changedWaveforms.empty())
        return;
    juce::RectangleList<int> dirty;
    for (const auto& [id, info] : clipInfos)
        if (info.source && info.source->peaks == nullptr && changedWaveforms.contains(info.source->key))
            if (const auto area = clipRect(info); !area.isEmpty())
                dirty.add(area);
    changedWaveforms.clear();
    dirty.consolidate();
    for (const auto& area : dirty)
        clipArea->repaint(area);
}

void Timeline::releaseUnusedWaveforms()
{
    std::erase_if(waveforms, [](const auto& item) { return !item.second.used; });
    for (auto& [key, waveform] : waveforms)
        waveform.used = false;
}

//==============================================================================
void Timeline::setPlayhead(SamplePosition position)
{
    if (position == playhead)
        return;
    const auto previous = playhead;
    playhead = position;
    if (followPlayhead && !overview)
    {
        const auto visible = getVisibleRange();
        if (position < visible.start || position >= visible.end)
        {
            repaintPlayhead(previous);
            scrollTo(position);
            repaintPlayhead(position);
            return;
        }
    }
    repaintPlayhead(previous);
    repaintPlayhead(position);
}

void Timeline::setFollowPlayhead(bool shouldFollow)
{
    followPlayhead = shouldFollow;
}

void Timeline::repaintPlayhead(SamplePosition position)
{
    const auto x = (overview ? overviewMapping() : mapping).toPixel(position);
    if (x < -clipMargin || x > clipAreaWidth() + clipMargin)
        return;
    const auto left = static_cast<int>(std::floor(x)) - playheadRepaintHalfWidth;
    clipArea->repaint(left, 0, (2 * playheadRepaintHalfWidth) + 1, clipArea->getHeight());
    ruler->repaint(left, 0, (2 * playheadRepaintHalfWidth) + 1, ruler->getHeight());
}

//==============================================================================
void Timeline::setWaveformScale(double scale)
{
    const auto next = std::clamp(std::isfinite(scale) ? scale : 1.0, minWaveformScale, maxWaveformScale);
    if (next == waveformScale)
        return;
    waveformScale = next;
    clipArea->repaint();
    if (onWaveformScaleChanged)
        onWaveformScaleChanged();
}

void Timeline::setTrackWaveformScale(TrackId track, std::optional<double> scale)
{
    if (scale)
        trackWaveformScales.insert_or_assign(track, std::clamp(std::isfinite(*scale) ? *scale : 1.0, minWaveformScale, maxWaveformScale));
    else
        trackWaveformScales.erase(track);
    if (const auto index = trackIndex(track); index >= 0)
        clipArea->repaint(laneBounds(index).getSmallestIntegerContainer());
    if (onWaveformScaleChanged)
        onWaveformScaleChanged();
}

double Timeline::getWaveformScaleFor(TrackId track) const
{
    const auto found = trackWaveformScales.find(track);
    return found != trackWaveformScales.end() ? found->second : waveformScale;
}

void Timeline::setRulerFormatter(RulerFormatter formatter)
{
    rulerFormatter = std::move(formatter);
    ruler->repaint();
}

void Timeline::setTrackHeaderFactory(std::function<std::unique_ptr<TrackHeader>()> factory)
{
    headerFactory = std::move(factory);
    headers->clear();
    headers->update();
}

void Timeline::setOverviewMode(bool shouldBeOverview)
{
    if (overview == shouldBeOverview)
        return;
    overview = shouldBeOverview;
    overviewSpans.clear();
    resized();
    repaint();
}

void Timeline::setHighlightedRange(SampleRange range)
{
    if (range == highlighted)
        return;
    highlighted = range;
    if (overview)
        clipArea->repaint();
}

//==============================================================================
void Timeline::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (overview)
        return;
    const auto action = wheel::actionFor(wheelModifiersFrom(event.mods), wheelDeltaFrom(wheel));
    const auto limits = getZoomLimits();
    const auto pointerX = static_cast<double>(event.getEventRelativeTo(clipArea.get()).position.x);
    auto next = mapping;
    if (action.scrollTimePixels != 0.0)
        next = next.scrolledBy(action.scrollTimePixels);
    if (action.zoomAtPointerSteps != 0.0)
        next = next.zoomedAround(limits.clamp(wheel::zoomedSamplesPerPixel(next.getSamplesPerPixel(), action.zoomAtPointerSteps, wheel::zoomFactorPerNotch)), pointerX);
    if (action.zoomAtEditCursorSteps != 0.0)
        next = next.zoomedAroundSample(limits.clamp(wheel::zoomedSamplesPerPixel(next.getSamplesPerPixel(), action.zoomAtEditCursorSteps, wheel::zoomFactorPerNotch)), getEditCursor(), clipAreaWidth());
    setMapping(next);
    if (action.waveformZoomSteps != 0.0)
        setWaveformScale(waveformScale * std::pow(wheel::waveformFactorPerStep, action.waveformZoomSteps));
    if (action.scrollTracksPixels != 0.0)
        setTrackScroll(trackScroll + juce::roundToInt(action.scrollTracksPixels));
}

void Timeline::mouseMagnify(const juce::MouseEvent& event, float scaleFactor)
{
    if (overview || !(scaleFactor > 0.0f))
        return;
    const auto pointerX = static_cast<double>(event.getEventRelativeTo(clipArea.get()).position.x);
    setMapping(mapping.zoomedAround(getZoomLimits().clamp(mapping.getSamplesPerPixel() / static_cast<double>(scaleFactor)), pointerX));
}

bool Timeline::keyPressed(const juce::KeyPress& key)
{
    if (overview)
        return false;
    const auto action = keyActionFor(key);
    switch (action)
    {
        case KeyAction::zoomIn:
            zoomAtEditCursor(1.0, wheel::zoomFactorPerKey);
            return true;
        case KeyAction::zoomOut:
            zoomAtEditCursor(-1.0, wheel::zoomFactorPerKey);
            return true;
        case KeyAction::zoomToFit:
            zoomToFit();
            return true;
        case KeyAction::waveformTaller:
            setWaveformScale(waveformScale * wheel::waveformFactorPerStep);
            return true;
        case KeyAction::waveformShorter:
            setWaveformScale(waveformScale / wheel::waveformFactorPerStep);
            return true;
        case KeyAction::scrollToStart:
            scrollTo(0);
            return true;
        case KeyAction::scrollToEnd:
            setMapping(mapping.withSampleAt(length, clipAreaWidth()));
            return true;
        case KeyAction::trackTaller:
        case KeyAction::trackShorter:
        {
            auto targets = getSelectedTracks();
            if (targets.empty() && keyboardCursor.track)
                targets.push_back(*keyboardCursor.track);
            if (targets.empty())
                return false;
            stepTrackHeights(targets, action == KeyAction::trackTaller ? 1 : -1);
            if (keyboardCursor.track)
                if (const auto index = trackIndex(*keyboardCursor.track); index >= 0)
                    revealTrack(index);
            return true;
        }
        case KeyAction::selectAll:
            selectAllClips();
            announce(juce::String{ static_cast<juce::int64>(selection.getClips().size()) } + " clips selected");
            return true;
        case KeyAction::cursorLeft:
            moveKeyboardCursor(CursorMove::left, SelectMode::replace);
            return true;
        case KeyAction::cursorRight:
            moveKeyboardCursor(CursorMove::right, SelectMode::replace);
            return true;
        case KeyAction::cursorUp:
            moveKeyboardCursor(CursorMove::up, SelectMode::replace);
            return true;
        case KeyAction::cursorDown:
            moveKeyboardCursor(CursorMove::down, SelectMode::replace);
            return true;
        case KeyAction::extendLeft:
            moveKeyboardCursor(CursorMove::left, SelectMode::add);
            return true;
        case KeyAction::extendRight:
            moveKeyboardCursor(CursorMove::right, SelectMode::add);
            return true;
        case KeyAction::extendUp:
            moveKeyboardCursor(CursorMove::up, SelectMode::add);
            return true;
        case KeyAction::extendDown:
            moveKeyboardCursor(CursorMove::down, SelectMode::add);
            return true;
        case KeyAction::cursorEnter:
            return moveKeyboardCursor(CursorMove::enter, SelectMode::replace);
        case KeyAction::cursorEscape:
            return moveKeyboardCursor(CursorMove::escape, SelectMode::replace);
        case KeyAction::none:
            break;
    }
    return false;
}

//==============================================================================
SamplePosition Timeline::getEditCursor() const
{
    if (const auto& range = selection.getTimeRange())
        return range->start;
    return playhead;
}

std::vector<ClipId> Timeline::getSelectedClips() const
{
    std::vector<ClipId> ids{ selection.getClips().begin(), selection.getClips().end() };
    std::ranges::sort(ids);
    return ids;
}

std::vector<TrackId> Timeline::getSelectedTracks() const
{
    std::vector<TrackId> ids;
    for (const auto& track : tracks)
        if (selection.contains(track.id))
            ids.push_back(track.id);
    return ids;
}

void Timeline::setSelectedClips(std::span<const ClipId> clips)
{
    if (selection.setClips(clips))
        selectionChanged();
}

void Timeline::setSelectedTracks(std::span<const TrackId> selectedTracks)
{
    if (selection.setTracks(selectedTracks))
        selectionChanged();
}

void Timeline::setTimeSelection(std::optional<SampleRange> range)
{
    if (range)
        range = range->intersection({ .start = 0, .end = length });
    if (selection.setTimeRange(range))
        selectionChanged();
}

void Timeline::selectClip(ClipId clip, SelectMode mode)
{
    const auto info = model.isComplete() ? model.clipInfo(clip) : std::nullopt;
    if (!info)
        return;
    const auto changed = selection.select(clip, mode);
    keyboardCursor = { .track = info->track, .clip = clip };
    cursorChanged();
    if (changed)
        selectionChanged();
}

void Timeline::selectTrack(TrackId track, SelectMode mode)
{
    if (trackIndex(track) < 0)
        return;
    const auto changed = selection.select(track, mode);
    keyboardCursor = { .track = track, .clip = std::nullopt };
    cursorChanged();
    if (changed)
        selectionChanged();
}

void Timeline::selectAllClips()
{
    if (!model.isComplete() || length <= 0)
        return;
    std::vector<ClipId> all;
    for (const auto& track : tracks)
    {
        const auto ids = model.clipsInRange(track.id, 0, length);
        all.insert(all.end(), ids.begin(), ids.end());
    }
    if (selection.setClips(all))
        selectionChanged();
}

void Timeline::clearSelection()
{
    const auto objects = selection.clearObjects();
    const auto time = selection.setTimeRange(std::nullopt);
    if (objects || time)
        selectionChanged();
}

void Timeline::zoomToSelection()
{
    if (const auto& range = selection.getTimeRange())
    {
        setVisibleRange(*range);
        return;
    }
    if (!model.isComplete())
        return;
    std::optional<SampleRange> span;
    for (const auto clip : selection.getClips())
    {
        const auto* cached = cachedClip(clip);
        const auto info = cached != nullptr ? std::optional{ *cached } : model.clipInfo(clip);
        if (!info)
            continue;
        const auto range = normalised(*info).range();
        span = span ? SampleRange{ .start = std::min(span->start, range.start), .end = std::max(span->end, range.end) } : range;
    }
    if (span)
        setVisibleRange(*span);
}

void Timeline::selectionChanged()
{
    clipArea->repaint();
    ruler->repaint();
    headers->repaintHeaders();
    if (onSelectionChanged)
        onSelectionChanged();
}

void Timeline::validateSelection(std::optional<std::span<const ClipId>> changedClips)
{
    auto changed = selection.retainTracks([this](TrackId track) { return trackIndex(track) >= 0; });
    const auto exists = [this](ClipId clip) { return model.isComplete() && model.clipInfo(clip).has_value(); };
    if (changedClips)
    {
        for (const auto clip : *changedClips)
            if (selection.contains(clip) && !exists(clip))
                changed = selection.retainClips([clip](ClipId id) { return id != clip; }) || changed;
    }
    else
    {
        changed = selection.retainClips(exists) || changed;
        if (const auto& range = selection.getTimeRange())
            changed = selection.setTimeRange(range->intersection({ .start = 0, .end = length })) || changed;
    }

    const auto cursorAffected = !changedClips || changedClips->empty() || std::ranges::any_of(*changedClips, [this](ClipId clip) { return keyboardCursor.clip == clip; });
    if (cursorAffected)
        validateCursor();
    if (changed)
        selectionChanged();
}

void Timeline::validateCursor()
{
    auto next = keyboardCursor;
    if (next.clip)
    {
        const auto info = model.isComplete() ? model.clipInfo(*next.clip) : std::nullopt;
        if (info)
            next.track = info->track;
        else
            next.clip.reset();
    }
    if (next.track && trackIndex(*next.track) < 0)
        next = {};
    if (next == keyboardCursor)
        return;
    keyboardCursor = next;
    cursorChanged();
}

void Timeline::cursorChanged()
{
    clipArea->repaint();
    headers->repaintHeaders();
}

void Timeline::setKeyboardCursor(KeyboardCursor cursor)
{
    if (cursor.clip)
    {
        const auto info = model.isComplete() ? model.clipInfo(*cursor.clip) : std::nullopt;
        if (!info)
            return;
        cursor.track = info->track;
    }
    if (cursor.track && trackIndex(*cursor.track) < 0)
        return;
    if (cursor == keyboardCursor)
        return;
    keyboardCursor = cursor;
    cursorChanged();
}

NavigationContext Timeline::navigationContext() const
{
    if (!model.isComplete())
        return {};
    return { .tracks = getTrackIds(),
        .firstVisibleTrack = layout.visibleTracks(trackScroll, clipAreaHeight()).begin,
        .visible = getVisibleRange(),
        .length = length,
        .clipsInRange = model.clipsInRange,
        .clipInfo = model.clipInfo,
        .adjacentClip = model.adjacentClip };
}

bool Timeline::moveKeyboardCursor(CursorMove move, SelectMode mode)
{
    if (overview)
        return false;
    const auto next = navigation::move(keyboardCursor, move, navigationContext());
    if (next == keyboardCursor || next.isEmpty())
        return false;
    keyboardCursor = next;
    auto changed = false;
    if (next.clip)
        changed = selection.select(*next.clip, mode);
    else if (next.track)
        changed = selection.select(*next.track, mode);
    cursorChanged();
    reveal(next);
    if (changed)
        selectionChanged();
    announce(describe(next));
    return true;
}

void Timeline::reveal(const KeyboardCursor& cursor)
{
    if (cursor.clip)
    {
        if (const auto info = model.clipInfo(*cursor.clip))
        {
            const auto range = normalised(*info).range();
            const auto visible = getVisibleRange();
            const auto span = visible.length();
            const auto margin = std::min(static_cast<SamplePosition>(static_cast<double>(span) * revealMarginFraction), std::max<SamplePosition>(0, (span - range.length()) / 2));
            if (range.length() <= span)
            {
                if (range.start < visible.start + margin)
                    setMapping(mapping.withSampleAt(range.start - margin, 0.0));
                else if (range.end > visible.end - margin)
                    setMapping(mapping.withSampleAt(range.end + margin, clipAreaWidth()));
            }
            else if (!range.intersects(visible))
            {
                setMapping(mapping.withSampleAt(range.start - margin, 0.0));
            }
        }
    }
    if (cursor.track)
        if (const auto index = trackIndex(*cursor.track); index >= 0)
            revealTrack(index);
}

void Timeline::revealTrack(int index)
{
    const auto top = layout.getTop(index);
    const auto bottom = top + layout.getHeight(index);
    const auto height = clipAreaHeight();
    if (top < trackScroll || bottom - top > height)
        setTrackScroll(top);
    else if (bottom > trackScroll + height)
        setTrackScroll(bottom - height);
}

std::vector<ClipId> Timeline::clipsInArea(juce::Rectangle<float> area) const
{
    std::vector<ClipId> ids;
    const auto local = clipArea->getLocalBounds().toFloat();
    const auto bounds = juce::Rectangle<float>::leftTopRightBottom(std::max(area.getX(), local.getX()), std::max(area.getY(), local.getY()), std::min(area.getRight(), local.getRight()), std::min(area.getBottom(), local.getBottom()));
    if (overview || !model.isComplete() || bounds.getWidth() < 0.0f || bounds.getHeight() < 0.0f || length <= 0)
        return ids;
    const auto from = std::max<SamplePosition>(mapping.sampleAt(static_cast<double>(bounds.getX())), 0);
    const auto to = std::min(std::max(mapping.sampleAt(static_cast<double>(bounds.getRight())) + 1, from + 1), length);
    if (from >= to)
        return ids;
    const auto top = static_cast<int>(std::floor(bounds.getY()));
    const auto span = layout.visibleTracks(trackScroll + top, std::max(1, static_cast<int>(std::ceil(bounds.getBottom())) - top));
    for (auto index = span.begin; index < span.end; ++index)
    {
        const auto found = model.clipsInRange(tracks[static_cast<std::size_t>(index)].id, from, to);
        ids.insert(ids.end(), found.begin(), found.end());
    }
    return ids;
}

juce::String Timeline::trackName(TrackId track) const
{
    const auto index = trackIndex(track);
    if (index < 0)
        return {};
    const auto& name = tracks[static_cast<std::size_t>(index)].info.name;
    return name.isNotEmpty() ? name : juce::String{ "Track " } + juce::String{ index + 1 };
}

juce::String Timeline::describeClip(const ClipInfo& info) const
{
    const auto range = normalised(info).range();
    auto text = trackName(info.track) + ", " + ruler::formatTime(static_cast<double>(range.start) / sampleRate, 0.001) + " to " + ruler::formatTime(static_cast<double>(range.end) / sampleRate, 0.001);
    if (info.kind == ClipKind::group)
        text << ", group";
    if (info.muted)
        text << ", muted";
    return text;
}

juce::String Timeline::describe(const KeyboardCursor& cursor) const
{
    if (cursor.clip)
        if (const auto info = model.clipInfo(*cursor.clip))
            return (info->name.isNotEmpty() ? info->name : juce::String{ "Untitled clip" }) + ", " + describeClip(*info) + (selection.contains(*cursor.clip) ? ", selected" : "");
    if (cursor.track)
        return trackName(*cursor.track) + ", track" + (selection.contains(*cursor.track) ? ", selected" : "");
    return {};
}

void Timeline::announce(const juce::String& text)
{
    if (text.isNotEmpty())
        juce::AccessibilityHandler::postAnnouncement(text, juce::AccessibilityHandler::AnnouncementPriority::medium);
}

void Timeline::focusFromClick()
{
    if (isShowing() || isOnDesktop())
        grabKeyboardFocus();
}

void Timeline::focusGained(FocusChangeType /*cause*/)
{
    if (!keyboardCursor.isEmpty())
        cursorChanged();
}

void Timeline::focusLost(FocusChangeType /*cause*/)
{
    if (!keyboardCursor.isEmpty())
        cursorChanged();
}

std::unique_ptr<juce::AccessibilityHandler> Timeline::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
}

//==============================================================================
bool Timeline::hasMarkerStrip() const
{
    return !overview && model.isComplete() && (model.markersInRange || model.loopRange);
}

int Timeline::rulerTotalHeight() const
{
    const auto m = metrics();
    return m.rulerHeight + (hasMarkerStrip() ? m.markerStripHeight : 0);
}

void Timeline::readMarkers()
{
    loop = model.isComplete() && model.loopRange ? model.loopRange() : std::nullopt;
    if (loop && loop->isEmpty())
        loop.reset();
    markerCache = {};
}

const std::vector<Marker>& Timeline::markersFor(SampleRange range)
{
    if (!model.isComplete() || !model.markersInRange)
    {
        markerCache = {};
        return markerCache.markers;
    }
    if (markerCache.valid && markerCache.queried.start <= range.start && markerCache.queried.end >= range.end)
        return markerCache.markers;
    const auto margin = range.length() / 2;
    markerCache.queried = { .start = range.start - margin, .end = range.end + margin };
    markerCache.markers = model.markersInRange(markerCache.queried.start, markerCache.queried.end);
    std::ranges::stable_sort(markerCache.markers, {}, &Marker::position);
    markerCache.valid = true;
    return markerCache.markers;
}

}

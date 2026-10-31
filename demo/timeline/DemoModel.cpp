#include "DemoModel.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace
{

const std::array trackColours{ juce::Colour{ 0xff3d8fd1 }, juce::Colour{ 0xff4caf7a }, juce::Colour{ 0xffd1913d }, juce::Colour{ 0xffb05cc8 }, juce::Colour{ 0xffd15a5a }, juce::Colour{ 0xff4fb3b8 } };

jtimeline::SamplePosition seconds(double value)
{
    return static_cast<jtimeline::SamplePosition>(value * DemoModel::getSampleRate());
}

}

DemoModel::DemoModel()
{
    formats.registerBasicFormats();
    makeSources();
    generate(Scenario::session);
}

jwaveform::WaveformSource DemoModel::syntheticSource(const juce::String& name, const SyntheticSettings& settings) const
{
    return { .key = name + " #" + juce::String{ generation } + (settings.slow ? " slow" : ""),
        .createReader = [settings] { return std::make_unique<SyntheticReader>(settings); },
        .peaks = nullptr };
}

void DemoModel::makeSources()
{
    ++generation;
    sources.clear();
    for (auto variant = 0; variant < 6; ++variant)
        sources.push_back(syntheticSource("synthetic " + juce::String{ variant }, { .sampleRate = sampleRate, .length = sourceLength, .variant = variant, .slow = slowBuilds, .failAt = -1 }));
}

void DemoModel::setSlowBuilds(bool slow)
{
    slowBuilds = slow;
    makeSources();
    generate(scenario);
}

void DemoModel::addStatusExamples()
{
    auto& track = makeTrack("Status examples");
    const auto add = [this, &track](double startSeconds, double lengthSeconds, const juce::String& name, jwaveform::WaveformSource source) {
        jtimeline::ClipInfo info;
        info.track = track.id;
        info.start = seconds(startSeconds);
        info.length = seconds(lengthSeconds);
        info.sourceLength = sourceLength;
        info.name = name;
        info.source = std::move(source);
        const auto id = jtimeline::ClipId{ nextId++ };
        clips.emplace(id, Clip{ .info = std::move(info) });
        track.byStart.push_back(id);
    };
    add(10.0, 20.0, "Offline file", { .key = "offline #" + juce::String{ generation }, .createReader = [] { return std::unique_ptr<juce::AudioFormatReader>{}; }, .peaks = nullptr });
    add(40.0, 20.0, "44.1 kHz source", syntheticSource("44.1 kHz", { .sampleRate = 44100.0, .length = sourceLength, .variant = 1, .slow = slowBuilds, .failAt = -1 }));
    add(70.0, 30.0, "Read error after 10 s", syntheticSource("read error", { .sampleRate = sampleRate, .length = sourceLength, .variant = 2, .slow = slowBuilds, .failAt = seconds(10.0) }));
    sortTrack(track);
}

int DemoModel::addFiles(const juce::Array<juce::File>& files)
{
    auto added = 0;
    auto longer = false;
    for (const auto& file : files)
    {
        const std::unique_ptr<juce::AudioFormatReader> reader{ formats.createReaderFor(file) };
        if (reader == nullptr || reader->lengthInSamples <= 0)
            continue;
        auto& track = makeTrack(file.getFileName());
        const auto path = file.getFullPathName();
        auto* manager = &formats;
        jtimeline::ClipInfo info;
        info.track = track.id;
        info.length = reader->lengthInSamples;
        info.sourceLength = reader->lengthInSamples;
        info.name = file.getFileNameWithoutExtension();
        info.source = jwaveform::WaveformSource{ .key = path + "|" + juce::String{ file.getLastModificationTime().toMilliseconds() },
            .createReader = [manager, path] { return std::unique_ptr<juce::AudioFormatReader>{ manager->createReaderFor(juce::File{ path }) }; },
            .peaks = nullptr };
        if (info.length > length)
        {
            length = info.length;
            longer = true;
        }
        const auto id = jtimeline::ClipId{ nextId++ };
        clips.emplace(id, Clip{ .info = std::move(info) });
        track.byStart.push_back(id);
        sortTrack(track);
        ++added;
    }
    if (longer)
        notifier.changed(jtimeline::FullReset{});
    else if (added > 0)
        notifier.changed(jtimeline::TracksChanged{});
    return added;
}

DemoModel::Track& DemoModel::makeTrack(const juce::String& name)
{
    const auto id = jtimeline::TrackId{ nextId++ };
    return tracks.emplace_back(Track{ .id = id, .info = { .name = name, .colour = trackColours.at(tracks.size() % trackColours.size()), .heightHint = std::nullopt }, .byStart = {}, .longest = 0 });
}

void DemoModel::randomiseLevels(jtimeline::ClipInfo& info)
{
    info.gainDb = random.nextInt(4) == 0 ? -18.0 + (random.nextDouble() * 24.0) : 0.0;
    info.fadeIn = random.nextBool() ? static_cast<jtimeline::SamplePosition>(random.nextDouble() * 0.3 * static_cast<double>(info.length)) : 0;
    info.fadeOut = random.nextBool() ? static_cast<jtimeline::SamplePosition>(random.nextDouble() * 0.3 * static_cast<double>(info.length)) : 0;
}

jtimeline::ClipInfo DemoModel::randomClip(jtimeline::TrackId track, jtimeline::SamplePosition start, jtimeline::SamplePosition clipLength, const juce::String& name)
{
    jtimeline::ClipInfo info;
    info.track = track;
    info.start = start;
    info.length = std::max<jtimeline::SamplePosition>(clipLength, 1);
    info.name = name;
    if (random.nextInt(20) == 0)
    {
        info.kind = jtimeline::ClipKind::group;
        info.name = "Group " + name;
    }
    info.muted = random.nextInt(20) == 0;
    if (random.nextInt(10) == 0)
        info.colour = juce::Colour::fromHSV(random.nextFloat(), 0.5f, 0.8f, 1.0f);
    if (info.kind == jtimeline::ClipKind::audio)
    {
        info.source = sources[static_cast<std::size_t>(random.nextInt(static_cast<int>(sources.size())))];
        info.sourceLength = sourceLength;
        info.sourceOffset = static_cast<jtimeline::SamplePosition>(random.nextDouble() * static_cast<double>(std::max<jtimeline::SamplePosition>(sourceLength - info.length, 0)));
        randomiseLevels(info);
    }
    return info;
}

void DemoModel::fillTrack(Track& track, double minSeconds, double maxSeconds, double maxGapSeconds)
{
    for (jtimeline::SamplePosition start = 0; start < length;)
    {
        const auto clipLength = std::min(seconds(minSeconds + (random.nextDouble() * (maxSeconds - minSeconds))), length - start);
        const auto id = jtimeline::ClipId{ nextId++ };
        clips.emplace(id, Clip{ .info = randomClip(track.id, start, clipLength, "Clip " + juce::String{ static_cast<juce::int64>(id) }) });
        track.byStart.push_back(id);
        const auto overlap = random.nextInt(10) == 0;
        start += overlap ? clipLength / 2 : clipLength + seconds(random.nextDouble() * maxGapSeconds);
    }
    sortTrack(track);
}

void DemoModel::generate(Scenario newScenario)
{
    scenario = newScenario;
    tracks.clear();
    clips.clear();
    switch (scenario)
    {
        case Scenario::session:
            length = seconds(3600.0);
            addStatusExamples();
            for (auto index = 0; index < 16; ++index)
                fillTrack(makeTrack("Track " + juce::String{ index + 1 }), 0.01, 20.0, 5.0);
            makeMarkers(12, { "Intro", "Verse 1", "Chorus 1", "Verse 2", "Chorus 2", "Bridge", "Solo", "Chorus 3", "Breakdown", "Outro", "Tag", "End" }, jtimeline::SampleRange{ .start = seconds(60.0), .end = seconds(90.0) });
            break;
        case Scenario::stress:
            length = seconds(3600.0);
            for (auto index = 0; index < 100; ++index)
                fillTrack(makeTrack("Track " + juce::String{ index + 1 }), 1.0, 25.0, 11.0);
            makeMarkers(1000, {}, jtimeline::SampleRange{ .start = seconds(600.0), .end = seconds(1200.0) });
            break;
        case Scenario::shortTimeline:
            length = seconds(2.0);
            for (auto index = 0; index < 8; ++index)
                fillTrack(makeTrack("Track " + juce::String{ index + 1 }), 0.00002, 0.2, 0.05);
            makeMarkers(4, { "A", "B", "C", "D" }, jtimeline::SampleRange{ .start = seconds(0.25), .end = seconds(0.75) });
            break;
        case Scenario::empty:
            length = 0;
            for (auto index = 0; index < 4; ++index)
                makeTrack("Track " + juce::String{ index + 1 });
            makeMarkers(0, {}, std::nullopt);
            break;
    }
    notifier.changed(jtimeline::FullReset{});
}

jtimeline::TimelineModel DemoModel::bind()
{
    return {
        .trackIds = [this] {
            std::vector<jtimeline::TrackId> ids;
            ids.reserve(tracks.size());
            for (const auto& track : tracks)
                ids.push_back(track.id);
            return ids; },
        .trackInfo = [this](jtimeline::TrackId id) -> std::optional<jtimeline::TrackInfo> {
            const auto* track = findTrack(id);
            return track != nullptr ? std::optional{ track->info } : std::nullopt;
        },
        .clipsInRange = [this](jtimeline::TrackId id, jtimeline::SamplePosition start, jtimeline::SamplePosition end) { return clipsInRange(id, start, end); },
        .clipInfo = [this](jtimeline::ClipId id) -> std::optional<jtimeline::ClipInfo> {
            const auto found = clips.find(id);
            return found != clips.end() ? std::optional{ found->second.info } : std::nullopt;
        },
        .sampleRate = [] { return sampleRate; },
        .length = [this] { return length; },
        .adjacentClip = [this](jtimeline::TrackId id, jtimeline::ClipId clip, jtimeline::Direction direction) { return adjacentClip(id, clip, direction); },
        .clipSpans = [this](jtimeline::TrackId id, jtimeline::SamplePosition start, jtimeline::SamplePosition end, jtimeline::SamplePosition minGap) {
            std::vector<jtimeline::SampleRange> spans;
            for (const auto clip : clipsInRange(id, start, end))
                spans.push_back(clips.at(clip).info.range().intersection({ .start = start, .end = end }));
            return jtimeline::mergeSpans(std::move(spans), minGap); },
        .markersInRange = [this](jtimeline::SamplePosition start, jtimeline::SamplePosition end) { return markersInRange(start, end); },
        .timelineSelection = [this] { return timelineSelection; },
        .notifier = &notifier,
    };
}

DemoModel::Track* DemoModel::findTrack(jtimeline::TrackId id)
{
    const auto found = std::ranges::find(tracks, id, &Track::id);
    return found != tracks.end() ? &*found : nullptr;
}

const DemoModel::Track* DemoModel::findTrack(jtimeline::TrackId id) const
{
    const auto found = std::ranges::find(tracks, id, &Track::id);
    return found != tracks.end() ? &*found : nullptr;
}

void DemoModel::sortTrack(Track& track)
{
    std::ranges::sort(track.byStart, [this](jtimeline::ClipId a, jtimeline::ClipId b) {
        const auto& left = clips.at(a).info;
        const auto& right = clips.at(b).info;
        return left.start != right.start ? left.start < right.start : a < b;
    });
    track.longest = 0;
    for (const auto id : track.byStart)
        track.longest = std::max(track.longest, clips.at(id).info.length);
}

std::vector<jtimeline::ClipId> DemoModel::clipsInRange(jtimeline::TrackId id, jtimeline::SamplePosition start, jtimeline::SamplePosition end) const
{
    std::vector<jtimeline::ClipId> result;
    const auto* track = findTrack(id);
    if (track == nullptr)
        return result;
    const auto upper = std::ranges::lower_bound(track->byStart, end, {}, [this](jtimeline::ClipId clip) { return clips.at(clip).info.start; });
    for (auto it = upper; it != track->byStart.begin();)
    {
        --it;
        const auto& info = clips.at(*it).info;
        if (info.start + track->longest <= start)
            break;
        if (info.start + info.length > start)
            result.push_back(*it);
    }
    std::ranges::reverse(result);
    return result;
}

std::optional<jtimeline::ClipId> DemoModel::adjacentClip(jtimeline::TrackId id, jtimeline::ClipId clip, jtimeline::Direction direction) const
{
    const auto* track = findTrack(id);
    if (track == nullptr)
        return std::nullopt;
    const auto info = clips.find(clip);
    if (info == clips.end())
        return std::nullopt;
    const auto key = [this](jtimeline::ClipId id) { return std::pair{ clips.at(id).info.start, id }; };
    const auto found = std::ranges::lower_bound(track->byStart, std::pair{ info->second.info.start, clip }, {}, key);
    if (found == track->byStart.end() || *found != clip)
        return std::nullopt;
    if (direction == jtimeline::Direction::previous)
        return found == track->byStart.begin() ? std::nullopt : std::optional{ *std::prev(found) };
    const auto next = std::next(found);
    return next == track->byStart.end() ? std::nullopt : std::optional{ *next };
}

std::vector<jtimeline::ClipId> DemoModel::clipsOnTracks(jtimeline::SampleRange range, std::size_t firstTrack, std::size_t count) const
{
    std::vector<jtimeline::ClipId> ids;
    for (auto index = firstTrack; index < std::min(tracks.size(), firstTrack + count); ++index)
    {
        const auto found = clipsInRange(tracks[index].id, range.start, range.end);
        ids.insert(ids.end(), found.begin(), found.end());
    }
    return ids;
}

std::vector<jtimeline::Marker> DemoModel::markersInRange(jtimeline::SamplePosition start, jtimeline::SamplePosition end) const
{
    const auto from = std::ranges::lower_bound(markers, start, {}, &jtimeline::Marker::position);
    const auto to = std::ranges::lower_bound(from, markers.end(), end, {}, &jtimeline::Marker::position);
    return { from, to };
}

void DemoModel::makeMarkers(int count, const juce::StringArray& names, std::optional<jtimeline::SampleRange> selection)
{
    markers.clear();
    for (auto index = 0; index < count; ++index)
    {
        const auto position = static_cast<jtimeline::SamplePosition>(static_cast<double>(length) * (index + 0.5) / count);
        const auto name = index < names.size() ? names[index] : "Marker " + juce::String{ index + 1 };
        markers.push_back({ .id = jtimeline::MarkerId{ static_cast<std::uint64_t>(index) + 1 }, .position = position, .name = name, .colour = index % 4 == 3 ? std::optional{ juce::Colour{ 0xff4fa3e0 } } : std::nullopt });
    }
    timelineSelection = selection;
}

jtimeline::ClipId DemoModel::addClip(jtimeline::ClipInfo info)
{
    const auto id = jtimeline::ClipId{ nextId++ };
    auto* track = findTrack(info.track);
    clips.emplace(id, Clip{ .info = std::move(info) });
    track->byStart.push_back(id);
    sortTrack(*track);
    return id;
}

void DemoModel::removeClip(jtimeline::ClipId id)
{
    const auto found = clips.find(id);
    if (found == clips.end())
        return;
    auto* track = findTrack(found->second.info.track);
    clips.erase(found);
    if (track != nullptr)
    {
        std::erase(track->byStart, id);
        sortTrack(*track);
    }
}

void DemoModel::editRandomClips(int count)
{
    if (tracks.empty() || length <= 0)
        return;
    std::vector<jtimeline::ClipId> changed;
    for (auto edit = 0; edit < count; ++edit)
    {
        auto& track = tracks[static_cast<std::size_t>(random.nextInt(static_cast<int>(tracks.size())))];
        const auto action = track.byStart.empty() ? 0 : random.nextInt(4);
        if (action == 0)
        {
            const auto clipLength = std::max<jtimeline::SamplePosition>(1, static_cast<jtimeline::SamplePosition>(random.nextDouble() * static_cast<double>(length) * 0.02));
            const auto start = static_cast<jtimeline::SamplePosition>(random.nextDouble() * static_cast<double>(length - std::min(clipLength, length)));
            changed.push_back(addClip(randomClip(track.id, start, std::min(clipLength, length - start), "New")));
            continue;
        }
        const auto id = track.byStart[static_cast<std::size_t>(random.nextInt(static_cast<int>(track.byStart.size())))];
        changed.push_back(id);
        if (action == 1)
        {
            removeClip(id);
            continue;
        }
        auto info = clips.at(id).info;
        if (action == 2)
        {
            const auto shift = static_cast<jtimeline::SamplePosition>((random.nextDouble() - 0.5) * static_cast<double>(info.length) * 4.0);
            info.start = std::clamp<jtimeline::SamplePosition>(info.start + shift, 0, length - info.length);
            info.track = tracks[static_cast<std::size_t>(random.nextInt(static_cast<int>(tracks.size())))].id;
        }
        else
        {
            info.colour = juce::Colour::fromHSV(random.nextFloat(), 0.6f, 0.9f, 1.0f);
            info.muted = !info.muted;
            if (info.kind == jtimeline::ClipKind::audio)
                randomiseLevels(info);
        }
        removeClip(id);
        clips.emplace(id, Clip{ .info = info });
        auto* target = findTrack(info.track);
        target->byStart.push_back(id);
        sortTrack(*target);
    }
    notifier.changed(jtimeline::ClipsChanged{ .ids = std::move(changed) });
}

void DemoModel::addTrack()
{
    auto& track = makeTrack("Added " + juce::String{ static_cast<juce::int64>(nextId) });
    if (length > 0)
        fillTrack(track, 0.5, 30.0, 10.0);
    notifier.changed(jtimeline::TracksChanged{});
}

void DemoModel::removeTrack()
{
    if (tracks.empty())
        return;
    for (const auto id : tracks.back().byStart)
        clips.erase(id);
    tracks.pop_back();
    notifier.changed(jtimeline::TracksChanged{});
}

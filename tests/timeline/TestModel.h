#pragma once

#include <jtimeline/jtimeline.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class TestModel
{
public:
    struct Counts
    {
        int clipInfo = 0;
        int clipsInRange = 0;
        int clipSpans = 0;
        int trackInfo = 0;
        int markers = 0;
    };

    std::vector<jtimeline::TrackId> tracks;
    std::map<jtimeline::ClipId, jtimeline::ClipInfo> clips;
    jtimeline::SamplePosition length = 48000LL * 60;
    std::unique_ptr<jtimeline::TimelineNotifier> notifier = std::make_unique<jtimeline::TimelineNotifier>();
    Counts counts;
    bool withSpans = true;
    bool withMarkers = false;
    std::vector<jtimeline::Marker> markers;
    std::optional<jtimeline::SampleRange> loop;

    jtimeline::TrackId addTrack()
    {
        tracks.push_back(jtimeline::TrackId{ tracks.size() + 1 });
        return tracks.back();
    }

    jtimeline::ClipId addClip(jtimeline::TrackId track, jtimeline::SamplePosition start, jtimeline::SamplePosition clipLength)
    {
        const auto id = jtimeline::ClipId{ clips.size() + 1000 };
        jtimeline::ClipInfo info;
        info.track = track;
        info.start = start;
        info.length = clipLength;
        clips[id] = info;
        return id;
    }

    jtimeline::TimelineModel bind()
    {
        jtimeline::TimelineModel model{
            .trackIds = [this] { return tracks; },
            .trackInfo = [this](jtimeline::TrackId id) -> std::optional<jtimeline::TrackInfo> {
                ++counts.trackInfo;
                if (std::ranges::find(tracks, id) == tracks.end())
                    return std::nullopt;
                jtimeline::TrackInfo info;
                info.name = "Track " + juce::String{ static_cast<int>(id) };
                return info;
            },
            .clipsInRange = [this](jtimeline::TrackId track, jtimeline::SamplePosition start, jtimeline::SamplePosition end) {
                ++counts.clipsInRange;
                std::vector<jtimeline::ClipId> result;
                for (const auto& [id, info] : clips)
                    if (info.track == track && info.start < end && info.start + info.length > start)
                        result.push_back(id);
                return result; },
            .clipInfo = [this](jtimeline::ClipId id) -> std::optional<jtimeline::ClipInfo> {
                ++counts.clipInfo;
                const auto found = clips.find(id);
                return found != clips.end() ? std::optional{ found->second } : std::nullopt;
            },
            .sampleRate = [] { return 48000.0; },
            .length = [this] { return length; },
            .adjacentClip = {},
            .clipSpans = {},
            .markersInRange = {},
            .loopRange = {},
            .notifier = notifier.get(),
        };
        if (withSpans)
        {
            model.clipSpans = [this](jtimeline::TrackId track, jtimeline::SamplePosition start, jtimeline::SamplePosition end, jtimeline::SamplePosition minGap) {
                ++counts.clipSpans;
                std::vector<jtimeline::SampleRange> spans;
                for (const auto& [id, info] : clips)
                    if (info.track == track)
                        spans.push_back(info.range().intersection({ .start = start, .end = end }));
                return jtimeline::mergeSpans(std::move(spans), minGap);
            };
        }
        if (withMarkers)
        {
            model.markersInRange = [this](jtimeline::SamplePosition start, jtimeline::SamplePosition end) {
                ++counts.markers;
                std::vector<jtimeline::Marker> result;
                for (const auto& marker : markers)
                    if (marker.position >= start && marker.position < end)
                        result.push_back(marker);
                return result;
            };
            model.loopRange = [this] { return loop; };
        }
        return model;
    }
};

#pragma once

#include "SyntheticSource.h"

#include <jtimeline/jtimeline.h>

#include <optional>
#include <unordered_map>
#include <vector>

class DemoModel
{
public:
    enum class Scenario {
        session,
        stress,
        shortTimeline,
        empty
    };

    DemoModel();

    void generate(Scenario newScenario);
    void setSlowBuilds(bool slow);
    int addFiles(const juce::Array<juce::File>& files);
    [[nodiscard]] jtimeline::TimelineModel bind();

    [[nodiscard]] std::vector<jtimeline::ClipId> clipsOnTracks(jtimeline::SampleRange range, std::size_t firstTrack, std::size_t count) const;
    void editRandomClips(int count);
    void addTrack();
    void removeTrack();

    [[nodiscard]] std::size_t getNumClips() const { return clips.size(); }
    [[nodiscard]] std::size_t getNumTracks() const { return tracks.size(); }
    [[nodiscard]] jtimeline::SamplePosition getLength() const { return length; }
    [[nodiscard]] static double getSampleRate() { return sampleRate; }

private:
    struct Track
    {
        jtimeline::TrackId id{};
        jtimeline::TrackInfo info;
        std::vector<jtimeline::ClipId> byStart;
        jtimeline::SamplePosition longest = 0;
    };

    struct Clip
    {
        jtimeline::ClipInfo info;
    };

    Track& makeTrack(const juce::String& name);
    jtimeline::ClipId addClip(jtimeline::ClipInfo info);
    void removeClip(jtimeline::ClipId id);
    void fillTrack(Track& track, double minSeconds, double maxSeconds, double maxGapSeconds);
    void sortTrack(Track& track);
    [[nodiscard]] Track* findTrack(jtimeline::TrackId id);
    [[nodiscard]] const Track* findTrack(jtimeline::TrackId id) const;
    [[nodiscard]] std::vector<jtimeline::ClipId> clipsInRange(jtimeline::TrackId id, jtimeline::SamplePosition start, jtimeline::SamplePosition end) const;
    [[nodiscard]] std::optional<jtimeline::ClipId> adjacentClip(jtimeline::TrackId id, jtimeline::ClipId clip, jtimeline::Direction direction) const;
    [[nodiscard]] std::vector<jtimeline::Marker> markersInRange(jtimeline::SamplePosition start, jtimeline::SamplePosition end) const;
    void makeMarkers(int count, const juce::StringArray& names, std::optional<jtimeline::SampleRange> selection);
    void makeSources();
    void randomiseLevels(jtimeline::ClipInfo& info);
    void addStatusExamples();
    [[nodiscard]] jwaveform::WaveformSource syntheticSource(const juce::String& name, const SyntheticSettings& settings) const;
    [[nodiscard]] jtimeline::ClipInfo randomClip(jtimeline::TrackId track, jtimeline::SamplePosition start, jtimeline::SamplePosition clipLength, const juce::String& name);

    static constexpr double sampleRate = 48000.0;
    static constexpr jtimeline::SamplePosition sourceLength = 48000LL * 600;

    std::vector<Track> tracks;
    std::unordered_map<jtimeline::ClipId, Clip> clips;
    std::vector<jtimeline::Marker> markers;
    std::optional<jtimeline::SampleRange> timelineSelection;
    jtimeline::SamplePosition length = 0;
    std::uint64_t nextId = 1;
    juce::Random random{ 7 };
    Scenario scenario = Scenario::session;
    bool slowBuilds = false;
    int generation = 0;
    std::vector<jwaveform::WaveformSource> sources;
    juce::AudioFormatManager formats;
    jtimeline::TimelineNotifier notifier;
};

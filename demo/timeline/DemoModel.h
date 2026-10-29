#pragma once

#include <jtimeline/jtimeline.h>

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

    void generate(Scenario scenario);
    [[nodiscard]] jtimeline::TimelineModel bind();

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
    [[nodiscard]] jtimeline::ClipInfo randomClip(jtimeline::TrackId track, jtimeline::SamplePosition start, jtimeline::SamplePosition clipLength, const juce::String& name);

    static constexpr double sampleRate = 48000.0;

    std::vector<Track> tracks;
    std::unordered_map<jtimeline::ClipId, Clip> clips;
    jtimeline::SamplePosition length = 0;
    std::uint64_t nextId = 1;
    juce::Random random{ 7 };
    jtimeline::TimelineNotifier notifier;
};

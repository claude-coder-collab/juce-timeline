#pragma once

#include "jwaveform_PeakData.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace jwaveform
{

//==============================================================================
/**
    Where a waveform comes from: either a key plus a reader factory, built through a PeakCache, or
    ready-made PeakData.

    The application owns file management: jwaveform never decides where audio lives.
*/
struct WaveformSource
{
    /** Identifies the audio, for example a file path plus its modification time. */
    juce::String key;
    /**
        Opens a new reader for the audio, or returns nullptr if it can't. Called on background
        threads, never the message thread, and possibly from several threads at once, so it must
        be thread-safe. Each call must return a new reader.
    */
    std::function<std::unique_ptr<juce::AudioFormatReader>()> createReader;
    /** Ready-made peaks; when set, the key and factory are ignored. */
    std::shared_ptr<const PeakData> peaks;
};

/** The state of a PeakHandle's waveform. */
enum class PeakState {
    none, /**< The handle refers to nothing. */
    loading, /**< Peaks are being built; getData() may already hold partial peaks. */
    ready, /**< The peaks are complete. */
    cannotOpen, /**< The reader factory returned nullptr or threw: for example a missing file. */
    readError /**< Reading failed part way; getData() holds the peaks read before the failure. */
};

namespace detail
{
struct PeakCacheEntry;
class PeakCacheImpl;
}

//==============================================================================
/**
    A request for one source's peaks. While any handle for a key exists, its build continues and
    its peaks are not evicted. Create handles with PeakCache::request(); use and destroy them on the
    message thread. A handle may outlive its cache, after which it still holds its peaks.
*/
class PeakHandle
{
public:
    /** Creates a handle that refers to nothing. */
    PeakHandle() = default;
    /** Releases the request. */
    ~PeakHandle();

    PeakHandle(const PeakHandle&) = delete;
    PeakHandle& operator=(const PeakHandle&) = delete;
    /** Moves a request. */
    PeakHandle(PeakHandle&& other) noexcept;
    /** Moves a request, releasing this one's. */
    PeakHandle& operator=(PeakHandle&& other) noexcept;

    /** Creates a handle that holds ready-made peaks, with no cache. */
    [[nodiscard]] static PeakHandle forPeaks(std::shared_ptr<const PeakData> peaks);

    /** Returns the peaks, possibly partial; nullptr until a build has opened the source. */
    [[nodiscard]] std::shared_ptr<const PeakData> getData() const;
    /** Returns the waveform's state. */
    [[nodiscard]] PeakState getState() const;

private:
    friend class PeakCache;
    void release();

    std::weak_ptr<detail::PeakCacheImpl> cache;
    std::shared_ptr<detail::PeakCacheEntry> entry;
    std::shared_ptr<const PeakData> ready;
    std::uint64_t listenerId = 0;
};

//==============================================================================
/**
    A shared, memory-bounded cache of waveform peaks, keyed by WaveformSource::key, with one build
    per key however many handles request it.

    Builds run on a thread pool owned by the cache. Progress is reported to handles' callbacks on
    the message thread, at most once per notification interval per key. A build is cancelled when
    no handle requests its key any more; partial and complete peaks stay cached, and a cancelled
    build resumes where it stopped when the key is requested again.

    The memory bound evicts the least recently requested peaks that no handle holds and no build is
    writing. Call its methods on the message thread. Share one cache between views of the same
    audio with std::shared_ptr.
*/
class PeakCache
{
public:
    /** Settings for a cache. */
    struct Options
    {
        std::size_t memoryLimitBytes = std::size_t{ 256 } * 1024 * 1024; /**< The memory bound. */
        int threads = 2; /**< The number of build threads. */
        int samplesPerPeak = defaultSamplesPerPeak; /**< Level-0 resolution of new peaks. */
        int notificationIntervalMs = 30; /**< The minimum time between progress callbacks for a key. */
    };

    /** Creates a cache. */
    explicit PeakCache(Options options);
    /** Creates a cache with default options. */
    PeakCache();
    /** Cancels and waits for every build. */
    ~PeakCache();

    PeakCache(const PeakCache&) = delete;
    PeakCache& operator=(const PeakCache&) = delete;
    PeakCache(PeakCache&&) = delete;
    PeakCache& operator=(PeakCache&&) = delete;

    /**
        Requests a source's peaks, starting or resuming its build if needed. onChange, if set, is
        called on the message thread when the peaks grow or the state changes, while the handle
        exists. A source with ready-made peaks returns a handle that holds them directly; a source
        with no key or no factory returns an empty handle.
    */
    [[nodiscard]] PeakHandle request(const WaveformSource& source, std::function<void()> onChange = {});

    /** Returns the bytes of peaks held. */
    [[nodiscard]] std::size_t getMemoryUsage() const;
    /** Changes the memory bound, evicting at once if needed. */
    void setMemoryLimit(std::size_t bytes);
    /** Returns the number of keys held. */
    [[nodiscard]] std::size_t getNumEntries() const;
    /** Returns true while any build is running. */
    [[nodiscard]] bool isBuilding() const;

private:
    std::shared_ptr<detail::PeakCacheImpl> impl;
};

}

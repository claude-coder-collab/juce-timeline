#include "jwaveform_PeakCache.h"

#include "jwaveform_PeakBuilder.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jwaveform
{

namespace detail
{

struct PeakCacheEntry
{
    std::function<std::unique_ptr<juce::AudioFormatReader>()> createReader;

    mutable std::mutex mutex;
    std::shared_ptr<PeakData> data;
    PeakState state = PeakState::loading;

    std::atomic<bool> cancel{ false };
    std::atomic<bool> building{ false };
    std::atomic<bool> changed{ false };

    std::map<std::uint64_t, std::function<void()>> listeners;
    std::uint64_t lastUsed = 0;

    [[nodiscard]] std::size_t memoryBytes() const
    {
        const std::scoped_lock lock{ mutex };
        return data != nullptr ? data->getMemoryBytes() : 0;
    }

    [[nodiscard]] PeakState getState() const
    {
        const std::scoped_lock lock{ mutex };
        return state;
    }
};

class PeakCacheImpl final : public std::enable_shared_from_this<PeakCacheImpl>, private juce::Timer
{
public:
    explicit PeakCacheImpl(const PeakCache::Options& optionsIn) :
        options(optionsIn),
        pool(juce::ThreadPoolOptions{}.withThreadName("jwaveform peaks").withNumberOfThreads(std::max(1, optionsIn.threads)))
    {
    }

    ~PeakCacheImpl() override
    {
        stopTimer();
        for (auto& [key, entry] : entries)
            entry->cancel = true;
        pool.removeAllJobs(true, -1);
    }

    PeakCacheImpl(const PeakCacheImpl&) = delete;
    PeakCacheImpl& operator=(const PeakCacheImpl&) = delete;
    PeakCacheImpl(PeakCacheImpl&&) = delete;
    PeakCacheImpl& operator=(PeakCacheImpl&&) = delete;

    std::pair<std::shared_ptr<PeakCacheEntry>, std::uint64_t> add(const WaveformSource& source, std::function<void()> onChange)
    {
        auto& entry = entries[source.key];
        if (entry == nullptr)
        {
            entry = std::make_shared<PeakCacheEntry>();
            entry->createReader = source.createReader;
        }
        entry->lastUsed = ++useCounter;
        const auto id = ++listenerCounter;
        entry->listeners.emplace(id, std::move(onChange));
        entry->cancel = false;
        resumeIfNeeded(entry);
        evict();
        return { entry, id };
    }

    void remove(const std::shared_ptr<PeakCacheEntry>& entry, std::uint64_t id)
    {
        entry->listeners.erase(id);
        if (entry->listeners.empty() && entry->building)
            entry->cancel = true;
        evict();
    }

    [[nodiscard]] std::size_t memoryUsage() const
    {
        std::size_t total = 0;
        for (const auto& [key, entry] : entries)
            total += entry->memoryBytes();
        return total;
    }

    void setMemoryLimit(std::size_t bytes)
    {
        options.memoryLimitBytes = bytes;
        evict();
    }

    [[nodiscard]] std::size_t numEntries() const { return entries.size(); }

    [[nodiscard]] bool isBuilding() const
    {
        return std::ranges::any_of(entries, [](const auto& item) { return item.second->building.load(); });
    }

private:
    void resumeIfNeeded(const std::shared_ptr<PeakCacheEntry>& entry)
    {
        if (!entry->listeners.empty() && !entry->building && entry->getState() == PeakState::loading)
            startBuild(entry);
        else if (entry->building && !isTimerRunning())
            startTimer(std::max(1, options.notificationIntervalMs));
    }

    void startBuild(const std::shared_ptr<PeakCacheEntry>& entry)
    {
        entry->cancel = false;
        entry->building = true;
        pool.addJob([entry, samplesPerPeak = options.samplesPerPeak] {
            build(*entry, samplesPerPeak);
            entry->changed = true;
            entry->building = false;
        });
        if (!isTimerRunning())
            startTimer(std::max(1, options.notificationIntervalMs));
    }

    static void build(PeakCacheEntry& entry, int samplesPerPeak)
    {
        std::unique_ptr<juce::AudioFormatReader> reader;
        try
        {
            if (entry.createReader)
                reader = entry.createReader();
        } catch (...)
        {
            reader = nullptr;
        }

        std::shared_ptr<PeakData> data;
        {
            const std::scoped_lock lock{ entry.mutex };
            if (reader == nullptr)
            {
                entry.state = PeakState::cannotOpen;
                return;
            }
            if (entry.data == nullptr)
                entry.data = PeakBuilder::createDataFor(*reader, samplesPerPeak);
            data = entry.data;
        }
        entry.changed = true;

        const auto result = PeakBuilder{ *reader, *data }.run(entry.cancel, [&entry] { entry.changed = true; });
        const std::scoped_lock lock{ entry.mutex };
        if (result == PeakBuilder::Result::complete)
            entry.state = PeakState::ready;
        else if (result == PeakBuilder::Result::readError)
            entry.state = PeakState::readError;
    }

    void timerCallback() override
    {
        const auto keepAlive = shared_from_this();
        std::vector<std::pair<std::shared_ptr<PeakCacheEntry>, std::uint64_t>> toNotify;
        for (auto& [key, entry] : entries)
        {
            if (entry->changed.exchange(false))
                for (const auto& [id, callback] : entry->listeners)
                    toNotify.emplace_back(entry, id);
            resumeIfNeeded(entry);
        }

        evict();
        const auto pending = std::ranges::any_of(entries, [](const auto& item) { return item.second->changed.load(); });
        if (!isBuilding() && !pending)
            stopTimer();

        for (const auto& [entry, id] : toNotify)
        {
            const auto found = entry->listeners.find(id);
            if (found != entry->listeners.end() && found->second)
                std::function<void()>{ found->second }();
        }
    }

    void evict()
    {
        const auto unused = [](const PeakCacheEntry& entry) { return entry.listeners.empty() && !entry.building; };
        std::erase_if(entries, [&unused](const auto& item) {
            const auto& entry = *item.second;
            if (!unused(entry))
                return false;
            const auto state = entry.getState();
            return state == PeakState::cannotOpen || state == PeakState::readError || entry.memoryBytes() == 0;
        });

        auto usage = memoryUsage();
        while (usage > options.memoryLimitBytes)
        {
            auto oldest = entries.end();
            for (auto it = entries.begin(); it != entries.end(); ++it)
                if (unused(*it->second) && (oldest == entries.end() || it->second->lastUsed < oldest->second->lastUsed))
                    oldest = it;
            if (oldest == entries.end())
                return;
            usage -= oldest->second->memoryBytes();
            entries.erase(oldest);
        }
    }

    PeakCache::Options options;
    std::unordered_map<juce::String, std::shared_ptr<PeakCacheEntry>> entries;
    std::uint64_t useCounter = 0;
    std::uint64_t listenerCounter = 0;
    juce::ThreadPool pool;
};

}

PeakHandle::~PeakHandle()
{
    release();
}

PeakHandle::PeakHandle(PeakHandle&& other) noexcept :
    cache(std::move(other.cache)),
    entry(std::move(other.entry)),
    ready(std::move(other.ready)),
    listenerId(std::exchange(other.listenerId, 0))
{
}

PeakHandle& PeakHandle::operator=(PeakHandle&& other) noexcept
{
    if (this != &other)
    {
        release();
        cache = std::move(other.cache);
        entry = std::move(other.entry);
        ready = std::move(other.ready);
        listenerId = std::exchange(other.listenerId, 0);
    }
    return *this;
}

void PeakHandle::release()
{
    if (entry != nullptr)
        if (const auto owner = cache.lock())
            owner->remove(entry, listenerId);
    cache.reset();
    entry.reset();
    ready.reset();
    listenerId = 0;
}

PeakHandle PeakHandle::forPeaks(std::shared_ptr<const PeakData> peaks)
{
    PeakHandle handle;
    handle.ready = std::move(peaks);
    return handle;
}

std::shared_ptr<const PeakData> PeakHandle::getData() const
{
    if (ready != nullptr)
        return ready;
    if (entry == nullptr)
        return nullptr;
    const std::scoped_lock lock{ entry->mutex };
    return entry->data;
}

PeakState PeakHandle::getState() const
{
    if (ready != nullptr)
        return ready->isComplete() ? PeakState::ready : PeakState::loading;
    if (entry == nullptr)
        return PeakState::none;
    return entry->getState();
}

PeakCache::PeakCache(Options options) :
    impl(std::make_shared<detail::PeakCacheImpl>(options))
{
}

PeakCache::PeakCache() :
    PeakCache(Options{})
{
}

PeakCache::~PeakCache() = default;

PeakHandle PeakCache::request(const WaveformSource& source, std::function<void()> onChange)
{
    JUCE_ASSERT_MESSAGE_THREAD;
    if (source.peaks != nullptr)
        return PeakHandle::forPeaks(source.peaks);
    PeakHandle handle;
    if (source.key.isEmpty() || !source.createReader)
        return handle;
    auto [entry, id] = impl->add(source, std::move(onChange));
    handle.cache = impl;
    handle.entry = std::move(entry);
    handle.listenerId = id;
    return handle;
}

std::size_t PeakCache::getMemoryUsage() const
{
    return impl->memoryUsage();
}

void PeakCache::setMemoryLimit(std::size_t bytes)
{
    impl->setMemoryLimit(bytes);
}

std::size_t PeakCache::getNumEntries() const
{
    return impl->numEntries();
}

bool PeakCache::isBuilding() const
{
    return impl->isBuilding();
}

}

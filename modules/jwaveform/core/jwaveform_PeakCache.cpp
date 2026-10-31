#include "jwaveform_PeakCache.h"

#include "jwaveform_PeakBuilder.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
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
    std::atomic<int> holders{ 0 };

    struct Block
    {
        std::shared_ptr<const SampleBlock> samples;
        std::uint64_t lastUsed = 0;
    };

    mutable std::mutex sampleMutex;
    std::map<SamplePosition, Block> blocks;
    std::size_t blockBytes = 0;
    bool samplesFailed = false;

    std::mutex readerMutex;
    std::unique_ptr<juce::AudioFormatReader> sampleReader;

    [[nodiscard]] std::size_t peakBytes() const
    {
        const std::scoped_lock lock{ mutex };
        return data != nullptr ? data->getMemoryBytes() : 0;
    }

    [[nodiscard]] std::size_t sampleBytes() const
    {
        const std::scoped_lock lock{ sampleMutex };
        return blockBytes;
    }

    [[nodiscard]] std::size_t memoryBytes() const { return peakBytes() + sampleBytes(); }

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
        pool(juce::ThreadPoolOptions{}.withThreadName("jwaveform peaks").withNumberOfThreads(std::max(1, optionsIn.threads))),
        samplePool(juce::ThreadPoolOptions{}.withThreadName("jwaveform samples").withNumberOfThreads(std::max(1, optionsIn.sampleThreads)))
    {
    }

    ~PeakCacheImpl() override
    {
        stopTimer();
        for (auto& [key, entry] : entries)
            entry->cancel = true;
        {
            const std::scoped_lock lock{ pendingMutex };
            pending.clear();
        }
        samplePool.removeAllJobs(true, -1);
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
        entry->holders = static_cast<int>(entry->listeners.size());
        entry->cancel = false;
        resumeIfNeeded(entry);
        evict();
        return { entry, id };
    }

    void remove(const std::shared_ptr<PeakCacheEntry>& entry, std::uint64_t id)
    {
        entry->listeners.erase(id);
        entry->holders = static_cast<int>(entry->listeners.size());
        if (entry->listeners.empty() && entry->building)
            entry->cancel = true;
        evict();
    }

    [[nodiscard]] int samplesPerPeak() const { return std::max(1, options.samplesPerPeak); }

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

    [[nodiscard]] std::size_t sampleMemoryUsage() const
    {
        std::size_t total = 0;
        for (const auto& [key, entry] : entries)
            total += entry->sampleBytes();
        return total;
    }

    [[nodiscard]] std::size_t numEntries() const { return entries.size(); }

    [[nodiscard]] bool isFetchingSamples() const { return sampleJobs > 0; }

    RawSamples samplesFor(const std::shared_ptr<PeakCacheEntry>& entry, SampleRange range)
    {
        std::shared_ptr<const PeakData> data;
        {
            const std::scoped_lock lock{ entry->mutex };
            data = entry->data;
        }
        if (data == nullptr || !entry->createReader)
            return {};
        range = range.intersection({ .start = 0, .end = data->getLength() });
        if (range.isEmpty())
            return {};

        const auto frames = SamplePosition{ blockFrames() };
        const auto first = range.start / frames;
        const auto last = std::min((range.end - 1) / frames, first + std::max(1, options.maxPendingSampleBlocks) - 1);
        std::vector<std::shared_ptr<const SampleBlock>> found;
        std::vector<SamplePosition> missing;
        {
            const std::scoped_lock lock{ entry->sampleMutex };
            if (entry->samplesFailed)
                return {};
            for (auto index = first; index <= last; ++index)
            {
                if (const auto block = entry->blocks.find(index); block != entry->blocks.end())
                {
                    block->second.lastUsed = ++useCounter;
                    found.push_back(block->second.samples);
                }
                else
                {
                    missing.push_back(index);
                }
            }
        }
        queue(entry, missing);
        return { data->getNumChannels(), std::move(found) };
    }

    [[nodiscard]] bool isBuilding() const
    {
        return std::ranges::any_of(entries, [](const auto& item) { return item.second->building.load(); });
    }

private:
    struct PendingBlock
    {
        std::weak_ptr<PeakCacheEntry> entry;
        SamplePosition index = 0;
        std::uint64_t stamp = 0;
    };

    [[nodiscard]] int blockFrames() const { return std::max(1, options.sampleBlockFrames); }

    void queue(const std::shared_ptr<PeakCacheEntry>& entry, std::span<const SamplePosition> indices)
    {
        if (indices.empty())
            return;
        auto added = 0;
        {
            const std::scoped_lock lock{ pendingMutex };
            for (const auto index : indices)
            {
                const auto same = [&entry, index](const PendingBlock& block) { return block.index == index && block.entry.lock() == entry; };
                if (const auto existing = std::ranges::find_if(pending, same); existing != pending.end())
                    pending.erase(existing);
                else
                    ++added;
                pending.push_back({ .entry = entry, .index = index, .stamp = ++useCounter });
            }
            while (std::cmp_greater(pending.size(), std::max(1, options.maxPendingSampleBlocks)))
                pending.pop_front();
        }
        for (auto i = 0; i < added; ++i)
        {
            ++sampleJobs;
            samplePool.addJob([this] {
                fetchNext();
                --sampleJobs;
            });
        }
        if (!isTimerRunning())
            startTimer(std::max(1, options.notificationIntervalMs));
    }

    void fetchNext()
    {
        PendingBlock next;
        {
            const std::scoped_lock lock{ pendingMutex };
            if (pending.empty())
                return;
            next = pending.back();
            pending.pop_back();
        }
        const auto entry = next.entry.lock();
        if (entry == nullptr)
            return;
        if (entry->holders == 0)
        {
            const std::scoped_lock lock{ entry->readerMutex };
            entry->sampleReader.reset();
            return;
        }

        std::shared_ptr<const PeakData> data;
        {
            const std::scoped_lock lock{ entry->mutex };
            data = entry->data;
        }
        {
            const std::scoped_lock lock{ entry->sampleMutex };
            if (data == nullptr || entry->samplesFailed || entry->blocks.contains(next.index))
                return;
        }

        const auto start = next.index * SamplePosition{ blockFrames() };
        const auto frames = static_cast<int>(std::min(SamplePosition{ blockFrames() }, data->getLength() - start));
        if (frames <= 0)
            return;
        auto block = std::make_shared<SampleBlock>(start, data->getNumChannels(), frames);
        const auto read = readBlock(*entry, *block);

        const std::scoped_lock lock{ entry->sampleMutex };
        if (!read)
        {
            entry->samplesFailed = true;
            return;
        }
        entry->blockBytes += block->getMemoryBytes();
        entry->blocks.insert_or_assign(next.index, PeakCacheEntry::Block{ .samples = std::move(block), .lastUsed = next.stamp });
        entry->changed = true;
    }

    static bool readBlock(PeakCacheEntry& entry, SampleBlock& block)
    {
        const std::scoped_lock lock{ entry.readerMutex };
        try
        {
            if (entry.sampleReader == nullptr)
                entry.sampleReader = entry.createReader();
            if (entry.sampleReader == nullptr)
                return false;
            juce::AudioBuffer<float> buffer{ block.getNumChannels(), block.getNumFrames() };
            if (!entry.sampleReader->read(&buffer, 0, block.getNumFrames(), block.getStart(), true, true))
                return false;
            for (auto channel = 0; channel < block.getNumChannels(); ++channel)
                std::ranges::copy(std::span{ buffer.getReadPointer(channel), static_cast<std::size_t>(block.getNumFrames()) }, block.getChannel(channel).begin());
            return true;
        } catch (...)
        {
            entry.sampleReader.reset();
            return false;
        }
    }

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
        const auto changed = std::ranges::any_of(entries, [](const auto& item) { return item.second->changed.load(); });
        if (!isBuilding() && !changed && sampleJobs == 0)
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
        if (usage > options.memoryLimitBytes)
            usage -= evictBlocks(usage - options.memoryLimitBytes);
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

    std::size_t evictBlocks(std::size_t excess)
    {
        std::size_t freed = 0;
        while (freed < excess)
        {
            PeakCacheEntry* oldestEntry = nullptr;
            auto oldestIndex = SamplePosition{ 0 };
            auto oldestUse = std::numeric_limits<std::uint64_t>::max();
            for (const auto& [key, entry] : entries)
            {
                const std::scoped_lock lock{ entry->sampleMutex };
                for (const auto& [index, block] : entry->blocks)
                {
                    if (block.lastUsed < oldestUse)
                    {
                        oldestUse = block.lastUsed;
                        oldestEntry = entry.get();
                        oldestIndex = index;
                    }
                }
            }
            if (oldestEntry == nullptr)
                break;
            const std::scoped_lock lock{ oldestEntry->sampleMutex };
            const auto found = oldestEntry->blocks.find(oldestIndex);
            if (found == oldestEntry->blocks.end())
                continue;
            const auto bytes = found->second.samples->getMemoryBytes();
            oldestEntry->blockBytes -= bytes;
            oldestEntry->blocks.erase(found);
            freed += bytes;
        }
        return freed;
    }

    PeakCache::Options options;
    std::unordered_map<juce::String, std::shared_ptr<PeakCacheEntry>> entries;
    std::uint64_t useCounter = 0;
    std::uint64_t listenerCounter = 0;
    std::mutex pendingMutex;
    std::deque<PendingBlock> pending;
    std::atomic<int> sampleJobs{ 0 };
    juce::ThreadPool pool;
    juce::ThreadPool samplePool;
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

RawSamples PeakHandle::getSamples(SampleRange range) const
{
    if (entry == nullptr)
        return {};
    if (const auto owner = cache.lock())
        return owner->samplesFor(entry, range);
    return {};
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

int PeakCache::getSamplesPerPeak() const
{
    return impl->samplesPerPeak();
}

std::size_t PeakCache::getSampleMemoryUsage() const
{
    return impl->sampleMemoryUsage();
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

bool PeakCache::isFetchingSamples() const
{
    return impl->isFetchingSamples();
}

}

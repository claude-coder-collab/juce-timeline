#include "../waveform/SyntheticReader.h"
#include "TestModel.h"

#include <jtimeline/component/jtimeline_TimelineParts.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using jtimeline::Timeline;
using jtimeline::TrackId;

namespace
{

constexpr jtimeline::SamplePosition second = 48000;

bool isMacPlatformForTests()
{
    return jtimeline::isMacPlatform();
}

juce::Image paint(Timeline& timeline)
{
    return timeline.createComponentSnapshot(timeline.getLocalBounds(), true, 1.0f);
}

bool runUntil(const std::function<bool()>& done, int timeoutMs = 10'000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (!done())
    {
        if (juce::Time::getMillisecondCounter() > end)
            return false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    }
    return true;
}

std::shared_ptr<const jwaveform::PeakData> readyPeaks(double rate, jtimeline::SamplePosition frames)
{
    auto data = std::make_shared<jwaveform::PeakData>(rate, frames, 1);
    const std::vector<float> samples(static_cast<std::size_t>(frames), 0.5f);
    const std::array channels{ samples.data() };
    data->append(channels, static_cast<int>(frames));
    data->complete();
    return data;
}

class RecordingLookAndFeel final : public juce::LookAndFeel_V4, public jtimeline::TimelineLookAndFeelMethods
{
public:
    struct Record
    {
        jtimeline::ClipId id{};
        jtimeline::WaveformStatus status = jtimeline::WaveformStatus::none;
        double sourceStart = 0.0;
        double sourceEnd = 0.0;
        bool hasPeaks = false;
    };

    void drawTimelineClip(juce::Graphics& g, const Timeline& timeline, const jtimeline::ClipPaint& clip) override
    {
        records[clip.id] = { .id = clip.id, .status = clip.waveform.status, .sourceStart = clip.waveform.sourceStart, .sourceEnd = clip.waveform.sourceEnd, .hasPeaks = clip.waveform.peaks != nullptr };
        TimelineLookAndFeelMethods::drawTimelineClip(g, timeline, clip);
    }

    std::map<jtimeline::ClipId, Record> records;
};

enum class Mouse {
    down,
    drag,
    up
};

void mouse(juce::Component& target, Mouse kind, juce::Point<float> position, juce::Point<float> downPosition, juce::ModifierKeys mods = {})
{
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent event{ juce::Desktop::getInstance().getMainMouseSource(), position, mods, juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, &target, &target, now, downPosition, now, 1, kind != Mouse::down && position != downPosition };
    if (kind == Mouse::down)
        target.mouseDown(event);
    else if (kind == Mouse::drag)
        target.mouseDrag(event);
    else
        target.mouseUp(event);
}

void click(juce::Component& target, juce::Point<float> position, juce::ModifierKeys mods = {})
{
    mouse(target, Mouse::down, position, position, mods);
    mouse(target, Mouse::up, position, position, mods);
}

void drag(juce::Component& target, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
{
    mouse(target, Mouse::down, from, from, mods);
    mouse(target, Mouse::drag, to, from, mods);
    mouse(target, Mouse::up, to, from, mods);
}

template <typename Part>
Part& part(Timeline& timeline)
{
    for (auto* child : timeline.getChildren())
        if (auto* found = dynamic_cast<Part*>(child))
            return *found;
    throw std::logic_error{ "missing part" };
}

class CountingHeader final : public jtimeline::TrackHeader
{
public:
    explicit CountingHeader(int& rebinds) :
        rebindCount(rebinds)
    {
    }

    void trackChanged() override { ++rebindCount; }

private:
    int& rebindCount; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

}

TEST_CASE("Timeline fits the whole timeline and clamps zoom", "[timeline]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    TestModel model;
    model.addTrack();
    Timeline timeline;
    timeline.setBounds(0, 0, 1172, 300);
    timeline.setModel(model.bind());
    const auto width = timeline.getClipAreaBounds().getWidth();
    REQUIRE(width == 1000);

    CHECK(timeline.getMapping().getSamplesPerPixel() == Approx(static_cast<double>(model.length) / 1000.0));
    CHECK(timeline.getVisibleRange().start == 0);

    timeline.setMapping(jtimeline::TimeMapping{ 1.0e-6, 0 });
    const auto deepest = timeline.getZoomLimits().minSamplesPerPixel;
    CHECK(timeline.getMapping().getSamplesPerPixel() == Approx(deepest));
    CHECK(deepest <= 64.0);
    CHECK(deepest >= 16.0);
    timeline.setMapping(jtimeline::TimeMapping{ 1.0e12, 0 });
    CHECK(timeline.getMapping().getSamplesPerPixel() == Approx(static_cast<double>(model.length) / 1000.0));

    timeline.setVisibleRange({ .start = 10 * second, .end = 20 * second });
    CHECK(timeline.getVisibleRange().start == 10 * second);
    CHECK(timeline.getVisibleRange().end == 20 * second);

    timeline.setMapping(timeline.getMapping().withSampleAt(-100 * second, 0.0));
    CHECK(timeline.getMapping().getStartPosition() == Approx(-1.0 * second));
    timeline.scrollTo(model.length);
    CHECK(timeline.getVisibleRange().end <= model.length + second);

    model.length = 0;
    timeline.refresh();
    timeline.zoomToFit();
    CHECK(timeline.getMapping().getSamplesPerPixel() == Approx(10.0 * 48000.0 / 1000.0));

    timeline.setPlayhead(5 * second);
    timeline.setVisibleRange({ .start = 0, .end = 10 * second });
    const auto before = timeline.getMapping().toPixel(5 * second);
    timeline.zoomAtAnchor(1.0, 2.0);
    CHECK(timeline.getMapping().toPixel(5 * second) == Approx(before));
    CHECK(timeline.getMapping().getSamplesPerPixel() == Approx(10.0 * 48000.0 / 2000.0));
}

TEST_CASE("Timeline reads only visible clips and re-queries changed ones", "[timeline]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    TestModel model;
    const auto track = model.addTrack();
    const auto first = model.addClip(track, 0, second);
    const auto far = model.addClip(track, 50 * second, second);
    Timeline timeline;
    timeline.setBounds(0, 0, 1172, 300);
    timeline.setModel(model.bind());
    timeline.setVisibleRange({ .start = 0, .end = 10 * second });
    model.counts = {};
    paint(timeline);
    CHECK(model.counts.clipsInRange == 1);
    CHECK(model.counts.clipInfo == 1);

    model.counts = {};
    paint(timeline);
    CHECK(model.counts.clipsInRange == 0);
    CHECK(model.counts.clipInfo == 0);

    const auto clipPoint = timeline.getClipAreaBounds().getTopLeft().toFloat() + juce::Point{ 50.0f, 10.0f };
    CHECK(timeline.clipAt(clipPoint) == first);
    CHECK_FALSE(timeline.clipAt(clipPoint + juce::Point{ 200.0f, 0.0f }));

    model.clips[first].start = 2 * second;
    model.notifier->changed(jtimeline::ClipsChanged{ .ids = { first } });
    CHECK_FALSE(timeline.clipAt(clipPoint));
    CHECK(timeline.clipAt(clipPoint + juce::Point{ 200.0f, 0.0f }) == first);
    model.counts = {};
    paint(timeline);
    CHECK(model.counts.clipsInRange == 1);

    model.clips.erase(far);
    model.notifier->changed(jtimeline::ClipsChanged{ .ids = { far } });
    model.clips.erase(first);
    model.notifier->changed(jtimeline::ClipsChanged{ .ids = { first } });
    CHECK_FALSE(timeline.clipAt(clipPoint + juce::Point{ 200.0f, 0.0f }));
    paint(timeline);
}

TEST_CASE("Timeline draws dense tracks and overviews from spans without clip queries", "[timeline]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    for (const auto withSpans : { true, false })
    {
        TestModel model;
        model.withSpans = withSpans;
        const auto track = model.addTrack();
        for (auto index = 0; index < 5000; ++index)
            model.addClip(track, static_cast<jtimeline::SamplePosition>(index) * 500, 100);
        Timeline timeline;
        timeline.setBounds(0, 0, 1172, 300);
        timeline.setModel(model.bind());
        model.counts = {};
        const auto image = paint(timeline);
        CHECK(model.counts.clipsInRange >= 1);
        CHECK(model.counts.clipSpans == (withSpans ? 1 : 0));
        if (withSpans)
            CHECK(model.counts.clipInfo == 0);
        const auto area = timeline.getClipAreaBounds();
        CHECK(image.getPixelAt(area.getX() + 10, area.getY() + 30) != image.getPixelAt(area.getRight() - 10, area.getY() + 30));

        Timeline overview;
        overview.setBounds(0, 0, 1000, 60);
        overview.setOverviewMode(true);
        overview.setModel(model.bind());
        overview.setHighlightedRange({ .start = 0, .end = second });
        model.counts = {};
        paint(overview);
        CHECK(model.counts.clipSpans == (withSpans ? 1 : 0));
        if (withSpans)
            CHECK(model.counts.clipInfo == 0);
        CHECK(overview.getClipAreaBounds() == overview.getLocalBounds());
        CHECK_FALSE(overview.clipAt({ 10.0f, 10.0f }));
    }
}

TEST_CASE("Timeline pools track headers for visible tracks", "[timeline]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    TestModel model;
    for (auto index = 0; index < 100; ++index)
        model.addTrack();
    Timeline timeline;
    auto created = 0;
    auto rebinds = 0;
    timeline.setTrackHeaderFactory([&created, &rebinds] {
        ++created;
        return std::make_unique<CountingHeader>(rebinds);
    });
    timeline.setBounds(0, 0, 1172, 28 + 12 + (72 * 5));
    timeline.setModel(model.bind());
    CHECK(created == 5);

    timeline.setTrackScroll(36);
    CHECK(created == 6);
    rebinds = 0;
    timeline.setTrackScroll(72 * 50);
    CHECK(created == 6);
    CHECK(rebinds == 5);
    CHECK(timeline.trackAt(static_cast<float>(timeline.getClipAreaBounds().getY()) + 1.0f) == TrackId{ 51 });

    const auto first = model.tracks.front();
    timeline.setTrackScroll(0);
    timeline.stepTrackHeights(std::vector{ first }, 1);
    CHECK(timeline.getTrackHeight(first) == 112);
    timeline.stepTrackHeights(std::vector{ first }, -1);
    timeline.stepTrackHeights(std::vector{ first }, -1);
    CHECK(timeline.getTrackHeight(first) == 48);
    timeline.setDefaultTrackHeight(30);
    CHECK(timeline.getTrackHeight(first) == 30);

    model.tracks.erase(model.tracks.begin());
    model.notifier->changed(jtimeline::TracksChanged{});
    CHECK(timeline.getTrackIds().size() == 99);
    CHECK(timeline.trackAt(static_cast<float>(timeline.getClipAreaBounds().getY()) + 1.0f) == TrackId{ 2 });
}

TEST_CASE("Timeline detaches when its notifier is destroyed", "[timeline]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    TestModel model;
    model.addTrack();
    Timeline first;
    Timeline other;
    first.setModel(model.bind());
    other.setModel(model.bind());
    CHECK(model.notifier->getNumListeners() == 2);
    other.setModel({});
    CHECK(model.notifier->getNumListeners() == 1);
    model.notifier.reset();
    CHECK(first.getTrackIds().empty());
    CHECK_FALSE(first.getModel().isComplete());
}

TEST_CASE("Timeline page follow jumps the playhead to the left edge", "[timeline]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    TestModel model;
    model.addTrack();
    Timeline timeline;
    timeline.setBounds(0, 0, 1172, 300);
    timeline.setModel(model.bind());
    timeline.setVisibleRange({ .start = 0, .end = 10 * second });
    timeline.setPlayhead(15 * second);
    CHECK(timeline.getVisibleRange().start == 0);
    timeline.setFollowPlayhead(true);
    timeline.setPlayhead(5 * second);
    CHECK(timeline.getVisibleRange().start == 0);
    timeline.setPlayhead(12 * second);
    CHECK(timeline.getVisibleRange().start == 12 * second);
}

TEST_CASE("Timeline waveform scale is clamped and reported", "[timeline]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    Timeline timeline;
    auto changes = 0;
    timeline.onWaveformScaleChanged = [&changes] { ++changes; };
    timeline.setWaveformScale(100.0);
    CHECK(timeline.getWaveformScale() == 64.0);
    timeline.setWaveformScale(0.0);
    CHECK(timeline.getWaveformScale() == 0.25);
    timeline.setTrackWaveformScale(TrackId{ 3 }, 2.0);
    CHECK(timeline.getWaveformScaleFor(TrackId{ 3 }) == 2.0);
    CHECK(timeline.getWaveformScaleFor(TrackId{ 4 }) == 0.25);
    timeline.setTrackWaveformScale(TrackId{ 3 }, std::nullopt);
    CHECK(timeline.getWaveformScaleFor(TrackId{ 3 }) == 0.25);
    CHECK(changes == 4);
}

TEST_CASE("Timeline draws clip waveforms and releases them when not drawn", "[timeline][waveform]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    TestModel model;
    const auto track = model.addTrack();
    const auto ready = model.addClip(track, second, 2 * second);
    const auto mismatch = model.addClip(track, 4 * second, second);
    const auto missing = model.addClip(track, 6 * second, second);
    const auto built = model.addClip(track, 8 * second, second);
    const auto group = model.addClip(track, 9 * second, second);
    model.clips[ready].sourceOffset = 10 * second;
    model.clips[ready].source = jwaveform::WaveformSource{ .key = {}, .createReader = {}, .peaks = readyPeaks(48000.0, 20 * second) };
    model.clips[mismatch].source = jwaveform::WaveformSource{ .key = {}, .createReader = {}, .peaks = readyPeaks(44100.0, second) };
    model.clips[missing].source = jwaveform::WaveformSource{ .key = "missing", .createReader = [] { return std::unique_ptr<juce::AudioFormatReader>{}; }, .peaks = nullptr };
    const auto synthetic = jwaveform::WaveformSource{ .key = "synthetic", .createReader = [] { return std::make_unique<SyntheticReader>(48000, 1, [](int, std::int64_t position) { return std::sin(static_cast<float>(position) * 0.01f); }); }, .peaks = nullptr };
    model.clips[built].source = synthetic;
    model.clips[group].kind = jtimeline::ClipKind::group;
    model.clips[group].source = synthetic;

    RecordingLookAndFeel lookAndFeel;
    Timeline timeline;
    timeline.setLookAndFeel(&lookAndFeel);
    timeline.setBounds(0, 0, 1172, 300);
    timeline.setModel(model.bind());
    timeline.setVisibleRange({ .start = 0, .end = 10 * second });
    paint(timeline);
    CHECK(lookAndFeel.records[ready].status == jtimeline::WaveformStatus::ready);
    CHECK(lookAndFeel.records[ready].sourceStart == Approx(10.0 * second));
    CHECK(lookAndFeel.records[ready].sourceEnd == Approx(12.0 * second));
    CHECK(lookAndFeel.records[mismatch].status == jtimeline::WaveformStatus::rateMismatch);
    CHECK_FALSE(lookAndFeel.records[mismatch].hasPeaks);
    CHECK(lookAndFeel.records[missing].status == jtimeline::WaveformStatus::none);
    CHECK(lookAndFeel.records[group].status == jtimeline::WaveformStatus::none);

    const auto cache = std::make_shared<jwaveform::PeakCache>();
    timeline.setPeakCache(cache);
    paint(timeline);
    CHECK(cache->getNumEntries() == 2);
    REQUIRE(runUntil([&] {
        paint(timeline);
        return lookAndFeel.records[missing].status == jtimeline::WaveformStatus::cannotOpen && lookAndFeel.records[built].status == jtimeline::WaveformStatus::ready;
    }));
    CHECK(lookAndFeel.records[built].hasPeaks);

    timeline.setTrackHeight(track, 8);
    lookAndFeel.records.clear();
    paint(timeline);
    CHECK(lookAndFeel.records[built].status == jtimeline::WaveformStatus::none);
    cache->setMemoryLimit(0);
    CHECK(cache->getNumEntries() == 0);

    timeline.setMapping(jtimeline::TimeMapping{ 1.0e-6, 0 });
    CHECK(timeline.getMapping().getSamplesPerPixel() == Approx(timeline.getZoomLimits().minSamplesPerPixel));
    CHECK(timeline.getZoomLimits().minSamplesPerPixel <= static_cast<double>(cache->getSamplesPerPeak()));
    timeline.setLookAndFeel(nullptr);
}

TEST_CASE("Timeline selects clips and tracks by clicking, rubber band and keyboard", "[timeline][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    TestModel model;
    const auto top = model.addTrack();
    const auto middle = model.addTrack();
    const auto bottom = model.addTrack();
    const auto a = model.addClip(top, 0, 2 * second);
    const auto b = model.addClip(top, 3 * second, 2 * second);
    const auto c = model.addClip(middle, second, 2 * second);
    const auto d = model.addClip(bottom, 6 * second, second);
    Timeline timeline;
    timeline.setVisible(true);
    timeline.setBounds(0, 0, 1172, 300);
    timeline.setModel(model.bind());
    timeline.setVisibleRange({ .start = 0, .end = 10 * second });
    auto changes = 0;
    timeline.onSelectionChanged = [&changes] { ++changes; };

    auto& area = part<jtimeline::detail::ClipArea>(timeline);
    const auto at = [](double seconds, int track) { return juce::Point{ static_cast<float>(seconds * 100.0), static_cast<float>((track * 72) + 30) }; };
    const auto shift = juce::ModifierKeys::shiftModifier;
    click(area, at(0.5, 0));
    CHECK(timeline.getSelectedClips() == std::vector{ a });
    CHECK(timeline.getSelectedTracks() == std::vector{ top });
    CHECK(timeline.getKeyboardCursor() == jtimeline::KeyboardCursor{ .track = top, .clip = a });
    click(area, at(3.5, 0), juce::ModifierKeys::commandModifier);
    CHECK(timeline.getSelectedClips() == std::vector{ a, b });
    click(area, at(0.5, 0), juce::ModifierKeys::commandModifier);
    CHECK(timeline.getSelectedClips() == std::vector{ b });
    click(area, at(8.0, 0));
    CHECK(timeline.getSelectedClips().empty());
    CHECK(timeline.getSelectedTracks().empty());
    CHECK(changes == 4);

    click(area, at(0.5, 0));
    click(area, at(6.5, 2), shift);
    CHECK(timeline.getSelectedClips() == std::vector{ a, b, c, d });
    CHECK(timeline.getSelectedTracks() == std::vector{ top, middle, bottom });
    CHECK(timeline.getKeyboardCursor().clip == a);
    click(area, at(2.5, 1), shift);
    CHECK(timeline.getSelectedClips() == std::vector{ a, c });
    CHECK(timeline.getSelectedTracks() == std::vector{ top, middle });
    drag(area, at(3.5, 0), at(6.5, 2), shift);
    CHECK(timeline.getSelectedClips() == std::vector{ a, b, c, d });
    CHECK(timeline.getKeyboardCursor().clip == a);

    drag(area, at(8.0, 0), at(2.5, 2));
    CHECK(timeline.getSelectedClips() == std::vector{ b, c, d });
    CHECK(timeline.getSelectedTracks() == std::vector{ top, middle, bottom });
    drag(area, at(8.0, 0), at(0.2, 0), shift);
    CHECK(timeline.getSelectedClips() == std::vector{ a, b, c, d });

    auto& header = dynamic_cast<jtimeline::TrackHeader&>(*timeline.getComponentAt(juce::Point{ 20, 28 + 72 + 10 }));
    click(header, { 10.0f, 10.0f });
    CHECK(timeline.getSelectedTracks() == std::vector{ middle });
    CHECK(timeline.getSelectedClips().empty());
    CHECK(timeline.getKeyboardCursor() == jtimeline::KeyboardCursor{ .track = middle, .clip = std::nullopt });
    auto& lastHeader = dynamic_cast<jtimeline::TrackHeader&>(*timeline.getComponentAt(juce::Point{ 20, 28 + 144 + 10 }));
    click(lastHeader, { 10.0f, 10.0f }, shift);
    CHECK(timeline.getSelectedTracks() == std::vector{ middle, bottom });
    CHECK(timeline.getKeyboardCursor().track == middle);

    timeline.stepTrackHeights(std::vector{ top, middle, bottom }, 0);
    CHECK(timeline.keyPressed(juce::KeyPress{ juce::KeyPress::upKey, isMacPlatformForTests() ? juce::ModifierKeys::ctrlModifier : juce::ModifierKeys::commandModifier, 0 }));
    CHECK(timeline.getTrackHeight(middle) == 112);
    CHECK(timeline.getTrackHeight(bottom) == 112);
    CHECK(timeline.getTrackHeight(top) == 72);

    CHECK(timeline.keyPressed(juce::KeyPress{ juce::KeyPress::returnKey }));
    CHECK(timeline.getSelectedClips() == std::vector{ c });
    CHECK(timeline.keyPressed(juce::KeyPress{ juce::KeyPress::upKey }));
    CHECK(timeline.getKeyboardCursor().clip == a);
    CHECK(timeline.keyPressed(juce::KeyPress{ juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0 }));
    CHECK(timeline.getSelectedClips() == std::vector{ a, b });
    CHECK(timeline.keyPressed(juce::KeyPress{ juce::KeyPress::escapeKey }));
    CHECK(timeline.getSelectedTracks() == std::vector{ top });
    CHECK_FALSE(timeline.keyPressed(juce::KeyPress{ juce::KeyPress::escapeKey }));

    timeline.setVisibleRange({ .start = 0, .end = 2 * second });
    timeline.setKeyboardCursor({ .track = std::nullopt, .clip = c });
    CHECK(timeline.getKeyboardCursor().track == middle);
    CHECK(timeline.keyPressed(juce::KeyPress{ juce::KeyPress::downKey }));
    CHECK(timeline.getKeyboardCursor() == jtimeline::KeyboardCursor{ .track = bottom, .clip = std::nullopt });
    CHECK(timeline.keyPressed(juce::KeyPress{ juce::KeyPress::rightKey }));
    CHECK(timeline.getKeyboardCursor().clip == std::nullopt);
    timeline.setKeyboardCursor({ .track = std::nullopt, .clip = a });
    CHECK(timeline.keyPressed(juce::KeyPress{ juce::KeyPress::rightKey }));
    CHECK(timeline.getKeyboardCursor().clip == b);
    CHECK(timeline.getVisibleRange().start <= 3 * second);
    CHECK(timeline.getVisibleRange().end >= 5 * second);
    CHECK(timeline.getSelectedTracks() == std::vector{ top });

    CHECK(timeline.keyPressed(juce::KeyPress{ 'a', juce::ModifierKeys::commandModifier, 0 }));
    CHECK(timeline.getSelectedClips() == std::vector{ a, b });
    timeline.clearSelection();
    CHECK(timeline.keyPressed(juce::KeyPress{ 'a', juce::ModifierKeys::commandModifier, 0 }));
    CHECK(timeline.getSelectedClips() == std::vector{ a, b, c, d });
    CHECK(timeline.getSelectedTracks() == std::vector{ top, middle, bottom });

    timeline.setTimeSelection(jtimeline::SampleRange{ .start = 2 * second, .end = 4 * second });
    CHECK(timeline.getEditCursor() == 2 * second);
    timeline.zoomToSelection();
    CHECK(timeline.getVisibleRange().start == 2 * second);
    timeline.setTimeSelection(std::nullopt);
    CHECK(timeline.getEditCursor() == timeline.getPlayhead());
    timeline.setSelectedClips(std::vector{ b, c });
    timeline.zoomToSelection();
    CHECK(timeline.getVisibleRange().start == second);
    CHECK(timeline.getVisibleRange().end == 5 * second);

    timeline.selectTrack(middle, jtimeline::SelectMode::replace);
    CHECK(timeline.keyPressed(juce::KeyPress{ 'a', juce::ModifierKeys::commandModifier, 0 }));
    CHECK(timeline.getSelectedClips() == std::vector{ c });
    CHECK(timeline.getSelectedTracks() == std::vector{ middle });

    timeline.setSelectedClips(std::vector{ d });
    timeline.setVisibleRange({ .start = 0, .end = 10 * second });
    CHECK(timeline.keyPressed(juce::KeyPress{ 'f', juce::ModifierKeys::altModifier, 0 }));
    CHECK(timeline.getVisibleRange().start == 6 * second);
    CHECK(timeline.getVisibleRange().end == 7 * second);

    timeline.setVisibleRange({ .start = 0, .end = 10 * second });
    timeline.setSelectedClips(std::vector{ c, d });
    const auto anchor = 4 * second;
    const auto before = timeline.getMapping().toPixel(anchor);
    const auto spp = timeline.getMapping().getSamplesPerPixel();
    const juce::MouseEvent event{ juce::Desktop::getInstance().getMainMouseSource(), { 700.0f, 100.0f }, juce::ModifierKeys::altModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &timeline, &timeline, juce::Time::getCurrentTime(), { 700.0f, 100.0f }, juce::Time::getCurrentTime(), 1, false };
    timeline.mouseWheelMove(event, juce::MouseWheelDetails{ .deltaX = 0.0f, .deltaY = 0.1f, .isReversed = false, .isSmooth = false, .isInertial = false });
    CHECK(timeline.getMapping().getSamplesPerPixel() < spp);
    CHECK(timeline.getMapping().toPixel(anchor) == Approx(before));
    CHECK(timeline.getZoomAnchor() == anchor);

    const auto keyBefore = timeline.getMapping().toPixel(anchor);
    CHECK(timeline.keyPressed(juce::KeyPress{ 't' }));
    CHECK(timeline.getMapping().toPixel(anchor) == Approx(keyBefore));

    CHECK_FALSE(timeline.keyPressed(juce::KeyPress{ juce::KeyPress::spaceKey }));
    auto toggles = 0;
    timeline.onPlayStop = [&toggles] { ++toggles; };
    CHECK(timeline.keyPressed(juce::KeyPress{ juce::KeyPress::spaceKey }));
    CHECK(toggles == 1);
}

TEST_CASE("Timeline keeps the selection across model changes while the ids exist", "[timeline][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    TestModel model;
    const auto top = model.addTrack();
    const auto middle = model.addTrack();
    const auto a = model.addClip(top, 0, second);
    const auto b = model.addClip(middle, 0, second);
    const auto c = model.addClip(middle, 2 * second, second);
    Timeline timeline;
    timeline.setBounds(0, 0, 1172, 300);
    timeline.setModel(model.bind());
    timeline.setSelectedClips(std::vector{ a, b, c });
    timeline.setSelectedTracks(std::vector{ top, middle });
    timeline.setKeyboardCursor({ .track = middle, .clip = b });
    timeline.setTimeSelection(jtimeline::SampleRange{ .start = 50 * second, .end = 200 * second });
    CHECK(timeline.getTimeSelection() == jtimeline::SampleRange{ .start = 50 * second, .end = 60 * second });
    auto changes = 0;
    timeline.onSelectionChanged = [&changes] { ++changes; };

    model.clips[a].start = 5 * second;
    model.notifier->changed(jtimeline::ClipsChanged{ .ids = { a } });
    CHECK(changes == 0);
    model.clips.erase(b);
    model.notifier->changed(jtimeline::ClipsChanged{ .ids = { b } });
    CHECK(timeline.getSelectedClips() == std::vector{ a, c });
    CHECK(timeline.getKeyboardCursor() == jtimeline::KeyboardCursor{ .track = middle, .clip = std::nullopt });
    CHECK(changes == 1);

    model.tracks.erase(model.tracks.begin());
    model.notifier->changed(jtimeline::TracksChanged{});
    CHECK(timeline.getSelectedTracks() == std::vector{ middle });

    model.clips.erase(a);
    model.length = 55 * second;
    model.notifier->changed(jtimeline::FullReset{});
    CHECK(timeline.getSelectedClips() == std::vector{ c });
    CHECK(timeline.getTimeSelection() == jtimeline::SampleRange{ .start = 50 * second, .end = 55 * second });
    CHECK(changes == 3);

    timeline.setModel({});
    CHECK(timeline.getSelection().getClips().empty());
    CHECK(timeline.getKeyboardCursor().isEmpty());
}

TEST_CASE("Timeline draws markers and the timeline selection, and selects time on the ruler", "[timeline][markers]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    TestModel model;
    model.addTrack();
    model.withMarkers = true;
    model.markers = { { .id = jtimeline::MarkerId{ 1 }, .position = 2 * second, .name = "Verse", .colour = std::nullopt }, { .id = jtimeline::MarkerId{ 2 }, .position = 30 * second, .name = "Chorus", .colour = juce::Colours::red } };
    model.timelineSelection = jtimeline::SampleRange{ .start = 4 * second, .end = 6 * second };
    Timeline timeline;
    timeline.setBounds(0, 0, 1172, 300);
    timeline.setModel(model.bind());
    timeline.setVisibleRange({ .start = 0, .end = 10 * second });
    CHECK(timeline.getClipAreaBounds().getY() == 28 + 18);
    CHECK(timeline.getTimelineSelection() == model.timelineSelection);

    model.counts = {};
    paint(timeline);
    CHECK(model.counts.markers == 1);
    paint(timeline);
    CHECK(model.counts.markers == 1);
    model.timelineSelection.reset();
    model.notifier->changed(jtimeline::MarkersChanged{});
    CHECK_FALSE(timeline.getTimelineSelection());
    paint(timeline);
    CHECK(model.counts.markers == 2);

    std::vector<jtimeline::SamplePosition> clicked;
    timeline.onTimeClicked = [&clicked](jtimeline::SamplePosition position) { clicked.push_back(position); };
    auto& ruler = part<jtimeline::detail::Ruler>(timeline);
    click(ruler, { 203.0f, 8.0f });
    CHECK(clicked == std::vector{ 2 * second });
    drag(ruler, { 100.0f, 30.0f }, { 300.0f, 30.0f });
    CHECK(clicked.back() == second);
    CHECK(timeline.getTimeSelection() == jtimeline::SampleRange{ .start = second, .end = 3 * second });
    paint(timeline);
    click(ruler, { 500.0f, 30.0f });
    CHECK_FALSE(timeline.getTimeSelection());
    CHECK(clicked.back() == 5 * second);

    model.withMarkers = false;
    timeline.setModel(model.bind());
    CHECK(timeline.getClipAreaBounds().getY() == 28);
}

TEST_CASE("Timeline exposes visible clips and tracks to accessibility clients", "[timeline][accessibility]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    TestModel model;
    const auto track = model.addTrack();
    const auto named = model.addClip(track, second, second);
    model.clips[named].name = "Take 1";
    const auto other = model.addClip(track, 3 * second, second);
    model.addClip(track, 50 * second, second);
    model.addClip(track, 5 * second, 10);
    Timeline timeline;
    timeline.setVisible(true);
    timeline.setBounds(0, 0, 1172, 300);
    timeline.addToDesktop(0);
    timeline.setModel(model.bind());
    timeline.setVisibleRange({ .start = 0, .end = 10 * second });

    auto& area = part<jtimeline::detail::ClipArea>(timeline);
    area.updateProxies();
    const auto proxies = area.getVisibleProxies();
    REQUIRE(proxies.size() == 2);
    CHECK(proxies[0]->getTitle() == "Take 1");
    CHECK(proxies[0]->getDescription() == "Track 1, 0:01.000 to 0:02.000");
    CHECK(proxies[0]->getBounds() == juce::Rectangle{ 100, 1, 100, 70 });
    CHECK(proxies[1]->getTitle() == "Untitled clip");
    CHECK(timeline.getComponentAt(juce::Point{ 160 + 150, 28 + 30 }) == &area);

    auto* handler = proxies[1]->getAccessibilityHandler();
    REQUIRE(handler != nullptr);
    CHECK_FALSE(handler->getCurrentState().isSelected());
    CHECK(handler->getActions().invoke(juce::AccessibilityActionType::press));
    CHECK(timeline.getSelectedClips() == std::vector{ other });
    CHECK(handler->getCurrentState().isSelected());
    auto* first = proxies[0]->getAccessibilityHandler();
    CHECK(first->getActions().invoke(juce::AccessibilityActionType::toggle));
    CHECK(timeline.getSelectedClips() == std::vector{ named, other });
    first->grabFocus();
    CHECK(timeline.getSelectedClips() == std::vector{ named, other });

    auto* timelineHandler = timeline.getAccessibilityHandler();
    REQUIRE(timelineHandler != nullptr);
    const auto children = timelineHandler->getChildren();
    REQUIRE(children.size() >= 2);
    CHECK(children[0]->getTitle() == "Track 1");
    CHECK(children[1]->getTitle() == "Track 1 clips");
    const auto clips = children[1]->getChildren();
    REQUIRE(clips.size() == 2);
    CHECK(clips[0]->getTitle() == "Take 1");
    CHECK(clips[0]->getParent() == children[1]);
    CHECK(children[1]->getParent() == timelineHandler);
    REQUIRE(clips[1]->getValueInterface() != nullptr);
    CHECK(clips[1]->getValueInterface()->getCurrentValueAsString() == "selected");
    CHECK(clips[0]->getValueInterface()->getCurrentValueAsString() == "selected");

    timeline.setKeyboardCursor({ .track = std::nullopt, .clip = named });
    CHECK(timeline.keyPressed(juce::KeyPress{ juce::KeyPress::rightKey }));
    CHECK(timeline.getKeyboardCursor().clip == other);
    CHECK(area.proxyFor(other)->getAccessibilityHandler()->hasFocus(false));
    CHECK(clips[0]->getValueInterface()->getCurrentValueAsString().isEmpty());
    const auto selectedBefore = timeline.getSelectedClips();
    CHECK(clips[0]->getActions().invoke(juce::AccessibilityActionType::focus));
    CHECK(timeline.getKeyboardCursor().clip == named);
    CHECK(timeline.getSelectedClips() == selectedBefore);

    timeline.setVisibleRange({ .start = 40 * second, .end = 60 * second });
    area.updateProxies();
    CHECK(area.getVisibleProxies().size() == 1);

    auto& header = dynamic_cast<jtimeline::TrackHeader&>(*timeline.getComponentAt(juce::Point{ 20, 40 }));
    CHECK(header.getTitle() == "Track 1");
    auto* trackHandler = header.getAccessibilityHandler();
    REQUIRE(trackHandler != nullptr);
    CHECK(trackHandler->getActions().invoke(juce::AccessibilityActionType::press));
    CHECK(timeline.getSelectedTracks() == std::vector{ track });
    CHECK(trackHandler->getCurrentState().isSelected());
}

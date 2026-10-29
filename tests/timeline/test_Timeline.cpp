#include "TestModel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using jtimeline::Timeline;
using jtimeline::TrackId;

namespace
{

constexpr jtimeline::SamplePosition second = 48000;

juce::Image paint(Timeline& timeline)
{
    return timeline.createComponentSnapshot(timeline.getLocalBounds(), true, 1.0f);
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
    CHECK(timeline.getMapping().getSamplesPerPixel() == Approx(1.0 / 16.0));
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
    timeline.zoomAtEditCursor(1.0, 2.0);
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

#include "SyntheticReader.h"

#include <jwaveform/view/jwaveform_WaveformDrawing.h>
#include <jwaveform/view/jwaveform_WaveformView.h>

#include <catch2/catch_test_macros.hpp>

#include <numbers>
#include <vector>

using jwaveform::PeakData;
using jwaveform::WaveformStyle;

namespace
{

std::shared_ptr<PeakData> squareData(float amplitude, int channels, jwaveform::SamplePosition length = 6400)
{
    auto data = std::make_shared<PeakData>(48000.0, length, channels);
    std::vector<float> samples(static_cast<std::size_t>(length));
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = i % 2 == 0 ? amplitude : -amplitude;
    std::vector<const float*> pointers(static_cast<std::size_t>(channels), samples.data());
    data->append(pointers, static_cast<int>(length));
    data->complete();
    return data;
}

juce::Image render(const PeakData& data, const WaveformStyle& style, int width = 100, int height = 100)
{
    juce::Image image{ juce::Image::ARGB, width, height, true };
    juce::Graphics g{ image };
    jwaveform::drawWaveform(g, { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) }, data, 0.0, static_cast<double>(data.getLength()), style);
    return image;
}

bool painted(const juce::Image& image, int x, int y)
{
    return image.getPixelAt(x, y).getAlpha() > 0;
}

}

TEST_CASE("Waveform drawing fills the amplitude around the lane centre", "[drawing]")
{
    const auto data = squareData(0.5f, 1);
    const auto image = render(*data, {});
    CHECK(painted(image, 50, 30));
    CHECK(painted(image, 50, 50));
    CHECK_FALSE(painted(image, 50, 20));
    CHECK_FALSE(painted(image, 50, 80));
}

TEST_CASE("Waveform display scale multiplies and clamps amplitude", "[drawing]")
{
    const auto data = squareData(0.5f, 1);
    WaveformStyle style;
    style.displayScale = 4.0;
    const auto image = render(*data, style);
    CHECK(painted(image, 50, 1));
    style.displayScale = 0.25;
    const auto small = render(*data, style);
    CHECK_FALSE(painted(small, 50, 40));
}

TEST_CASE("Waveform envelope silences fades", "[drawing]")
{
    const auto data = squareData(0.9f, 1);
    WaveformStyle style;
    style.envelope.fadeIn = { .start = 0, .end = 3200 };
    const auto image = render(*data, style);
    CHECK_FALSE(painted(image, 1, 20));
    CHECK(painted(image, 90, 20));
}

TEST_CASE("Waveform lanes stack channels until lanes get too short", "[drawing]")
{
    const WaveformStyle stacked;
    CHECK(jwaveform::laneCountFor(2, 100.0f, stacked) == 2);
    CHECK(jwaveform::laneCountFor(2, 20.0f, stacked) == 1);
    CHECK(jwaveform::laneCountFor(1, 100.0f, stacked) == 1);
    WaveformStyle combined;
    combined.channelMode = jwaveform::ChannelMode::combined;
    CHECK(jwaveform::laneCountFor(8, 1000.0f, combined) == 1);

    const auto data = squareData(0.5f, 2);
    const auto image = render(*data, stacked);
    CHECK(painted(image, 50, 15));
    CHECK_FALSE(painted(image, 50, 45));
    CHECK(painted(image, 50, 85));
}

TEST_CASE("Waveform drawing only covers the clip region", "[drawing]")
{
    const auto data = squareData(0.9f, 1);
    const juce::Image image{ juce::Image::ARGB, 100, 100, true };
    {
        juce::Graphics g{ image };
        g.reduceClipRegion(40, 0, 20, 100);
        jwaveform::drawWaveform(g, { -1000.0f, 0.0f, 2000.0f, 100.0f }, *data, 0.0, 6400.0, {});
    }
    CHECK(painted(image, 45, 50));
    CHECK_FALSE(painted(image, 30, 50));
    CHECK_FALSE(painted(image, 70, 50));
}

TEST_CASE("WaveformView shows ready-made and cached peaks", "[view]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    jwaveform::WaveformView view;
    view.setBounds(0, 0, 200, 80);
    CHECK(view.getState() == jwaveform::PeakState::none);

    view.setSource({ .key = {}, .createReader = {}, .peaks = squareData(0.5f, 1) });
    CHECK(view.getState() == jwaveform::PeakState::ready);
    CHECK(painted(view.createComponentSnapshot(view.getLocalBounds(), true, 1.0f), 100, 40));

    const auto cache = std::make_shared<jwaveform::PeakCache>();
    view.setPeakCache(cache);
    view.setSource({ .key = "synthetic", .createReader = [] { return std::make_unique<SyntheticReader>(48000, 1, [](int, std::int64_t) { return 0.5f; }); }, .peaks = nullptr });
    const auto end = juce::Time::getMillisecondCounter() + 10'000;
    while (view.getState() == jwaveform::PeakState::loading && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    CHECK(view.getState() == jwaveform::PeakState::ready);

    view.clearSource();
    CHECK(view.getState() == jwaveform::PeakState::none);
    CHECK_FALSE(view.resolveColour(jwaveform::WaveformView::waveformColourId).isTransparent());
    view.setColour(jwaveform::WaveformView::waveformColourId, juce::Colours::red);
    CHECK(view.resolveColour(jwaveform::WaveformView::waveformColourId) == juce::Colours::red);
}

TEST_CASE("Waveform columns are snapped to physical pixels at fractional positions and scales", "[drawing]")
{
    const auto data = squareData(0.3f, 1);
    for (const auto scale : { 1.0f, 2.0f })
    {
        const auto size = static_cast<int>(100.0f * scale);
        const juce::Image image{ juce::Image::ARGB, size, size, true };
        {
            juce::Graphics g{ image };
            g.addTransform(juce::AffineTransform::scale(scale));
            WaveformStyle style;
            style.colour = juce::Colours::white;
            jwaveform::drawWaveform(g, { 10.3f, 5.7f, 80.6f, 60.2f }, *data, 0.0, static_cast<double>(data->getLength()), style);
        }
        auto paintedCount = 0;
        auto partial = 0;
        for (auto y = 0; y < size; ++y)
            for (auto x = 0; x < size; ++x)
                if (const auto alpha = image.getPixelAt(x, y).getAlpha(); alpha > 0)
                    ++(alpha == 255 ? paintedCount : partial);
        CHECK(paintedCount > 0);
        CHECK(partial == 0);
    }
}

namespace
{

jwaveform::RawSamples constantSamples(float value, jwaveform::SamplePosition length)
{
    const auto block = std::make_shared<jwaveform::SampleBlock>(0, 1, static_cast<int>(length));
    std::ranges::fill(block->getChannel(0), value);
    return { 1, { block } };
}

juce::Image renderSpan(const PeakData& data, double start, double end, const jwaveform::RawSamples& samples)
{
    juce::Image image{ juce::Image::ARGB, 100, 100, true };
    juce::Graphics g{ image };
    jwaveform::drawWaveform(g, { 0.0f, 0.0f, 100.0f, 100.0f }, data, start, end, {}, samples);
    return image;
}

}

TEST_CASE("Waveform drawing uses raw samples below one peak per pixel", "[drawing][samples]")
{
    const auto data = squareData(0.9f, 1);
    CHECK(jwaveform::usesRawSamples(*data, 2.0));
    CHECK_FALSE(jwaveform::usesRawSamples(*data, 64.0));

    const auto stretched = renderSpan(*data, 0.0, 200.0, {});
    CHECK(painted(stretched, 50, 10));
    const auto raw = renderSpan(*data, 0.0, 200.0, constantSamples(0.2f, 6400));
    CHECK_FALSE(painted(raw, 50, 10));
    CHECK(painted(raw, 50, 40));

    const auto zoomedOut = renderSpan(*data, 0.0, 6400.0, constantSamples(0.2f, 6400));
    CHECK(painted(zoomedOut, 50, 10));

    const auto partial = renderSpan(*data, 0.0, 200.0, constantSamples(0.2f, 100));
    CHECK_FALSE(painted(partial, 20, 10));
    CHECK(painted(partial, 80, 10));
}

TEST_CASE("Waveform drawing draws a line with dots through wide samples", "[drawing][samples]")
{
    const auto data = squareData(0.9f, 1);
    const auto image = renderSpan(*data, 0.0, 10.0, constantSamples(0.5f, 6400));
    CHECK(painted(image, 50, 25));
    CHECK_FALSE(painted(image, 50, 10));
    CHECK_FALSE(painted(image, 50, 75));
    CHECK(painted(image, 5, 23));
    CHECK_FALSE(painted(image, 10, 23));

    const auto closer = renderSpan(*data, 0.0, 50.0, constantSamples(0.5f, 6400));
    CHECK(painted(closer, 50, 25));
    CHECK_FALSE(painted(closer, 51, 23));
}

TEST_CASE("Waveform drawing shows peaks between samples", "[drawing][samples]")
{
    const auto data = squareData(0.9f, 1);
    auto block = std::make_shared<jwaveform::SampleBlock>(0, 1, 6400);
    const auto values = block->getChannel(0);
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = static_cast<float>(0.5 * std::sin((std::numbers::pi / 2.0 * static_cast<double>(i)) + (std::numbers::pi / 4.0)));
    const jwaveform::RawSamples samples{ 1, { std::move(block) } };
    const auto image = renderSpan(*data, 3200.0, 3210.0, samples);
    auto top = 100;
    for (auto x = 0; x < 100; ++x)
        for (auto y = 0; y < 100; ++y)
            if (painted(image, x, y))
                top = std::min(top, y);
    CHECK(top <= 26);
    CHECK(top >= 23);
}

TEST_CASE("WaveformView fetches raw samples when zoomed past the peaks", "[view][samples]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    jwaveform::WaveformView view;
    view.setBounds(0, 0, 200, 80);
    const auto cache = std::make_shared<jwaveform::PeakCache>();
    view.setPeakCache(cache);
    view.setSource({ .key = "synthetic", .createReader = [] { return std::make_unique<SyntheticReader>(48000, 1, [](int, std::int64_t) { return 0.5f; }); }, .peaks = nullptr });
    const auto runFor = [](const std::function<bool()>& done) {
        const auto end = juce::Time::getMillisecondCounter() + 10'000;
        while (!done() && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        return done();
    };
    REQUIRE(runFor([&] { return view.getState() == jwaveform::PeakState::ready; }));

    static_cast<void>(view.createComponentSnapshot(view.getLocalBounds(), true, 1.0f));
    CHECK(cache->getSampleMemoryUsage() == 0);
    view.setVisibleRange(100.0, 300.0);
    CHECK(runFor([&] {
        static_cast<void>(view.createComponentSnapshot(view.getLocalBounds(), true, 1.0f));
        return cache->getSampleMemoryUsage() > 0;
    }));
}

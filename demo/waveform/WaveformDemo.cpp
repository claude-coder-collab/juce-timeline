#include "WaveformDemo.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <numbers>
#include <thread>

namespace
{

class SyntheticReader final : public juce::AudioFormatReader
{
public:
    SyntheticReader(double rate, juce::int64 length, bool slowIn) :
        AudioFormatReader(nullptr, "Synthetic"),
        slow(slowIn)
    {
        sampleRate = rate;
        lengthInSamples = length;
        numChannels = 2;
        bitsPerSample = 32;
        usesFloatingPointData = true;
    }

    bool readSamples(int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer, juce::int64 startSampleInFile, int numSamples) override
    {
        if (slow)
            std::this_thread::sleep_for(std::chrono::milliseconds{ 15 });
        for (auto channel = 0; channel < numDestChannels; ++channel)
        {
            if (destChannels[channel] == nullptr)
                continue;
            for (auto i = 0; i < numSamples; ++i)
            {
                const auto value = generate(channel, startSampleInFile + i);
                std::memcpy(destChannels[channel] + startOffsetInDestBuffer + i, &value, sizeof(float));
            }
        }
        return true;
    }

private:
    [[nodiscard]] float generate(int channel, juce::int64 position) const
    {
        const auto seconds = static_cast<double>(position) / sampleRate;
        const auto bar = std::fmod(seconds, 8.0);
        const auto envelope = bar < 4.0 ? std::exp(-bar * 1.5) : 0.15 * (1.0 + std::sin(seconds * 0.7));
        const auto slowSwell = 0.55 + (0.45 * std::sin(seconds * 2.0 * std::numbers::pi / 600.0));
        const auto frequency = channel == 0 ? 110.0 : 165.0;
        const auto tone = std::sin(2.0 * std::numbers::pi * frequency * seconds);
        const auto hash = static_cast<std::uint32_t>(position * 2654435761LL) ^ static_cast<std::uint32_t>(channel * 40503);
        const auto noise = (static_cast<double>(hash % 2001U) / 1000.0) - 1.0;
        return static_cast<float>(slowSwell * envelope * ((0.8 * tone) + (0.2 * noise)));
    }

    bool slow = false;
};

constexpr int toolbarHeight = 30;
constexpr int overviewHeight = 70;

}

class WaveformDemo::Overview final : public juce::Component
{
public:
    explicit Overview(WaveformDemo& ownerIn) :
        owner(ownerIn)
    {
        view.setInterceptsMouseClicks(false, false);
        view.setColour(jwaveform::WaveformView::backgroundColourId, juce::Colours::black.withAlpha(0.2f));
        addAndMakeVisible(view);
    }

    [[nodiscard]] jwaveform::WaveformView& getView() { return view; }

    void resized() override { view.setBounds(getLocalBounds()); }

    void paintOverChildren(juce::Graphics& g) override
    {
        const auto length = owner.sourceLength();
        if (length <= 0.0)
            return;
        const auto width = static_cast<double>(getWidth());
        const auto start = static_cast<double>(owner.mapping.getStartSample()) + owner.mapping.getStartFraction();
        const auto end = start + (static_cast<double>(owner.mainView.getWidth()) * owner.mapping.getSamplesPerPixel());
        const auto left = static_cast<float>(std::clamp(start / length * width, -2.0, width + 2.0));
        const auto right = static_cast<float>(std::clamp(end / length * width, -2.0, width + 2.0));
        const auto area = juce::Rectangle<float>::leftTopRightBottom(left, 0.0f, std::max(right, left + 2.0f), static_cast<float>(getHeight()));
        g.setColour(juce::Colours::orange.withAlpha(0.2f));
        g.fillRect(area);
        g.setColour(juce::Colours::orange);
        g.drawRect(area, 1.0f);
    }

    void mouseDown(const juce::MouseEvent& event) override { mouseDrag(event); }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        owner.centreOn(static_cast<double>(event.position.x) / static_cast<double>(std::max(1, getWidth())) * owner.sourceLength());
    }

private:
    WaveformDemo& owner;
    jwaveform::WaveformView view;
};

WaveformDemo::WaveformDemo() :
    cache(std::make_shared<jwaveform::PeakCache>()),
    overview(std::make_unique<Overview>(*this))
{
    formats.registerBasicFormats();
    mainView.setPeakCache(cache);
    mainView.setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(true);
    overview->getView().setPeakCache(cache);

    openButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Open audio", juce::File{}, formats.getWildcardForAllFormats());
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile())
                openFile(fc.getResult());
        });
    };
    syntheticButton.onClick = [this] { openSynthetic(); };

    channelMode.addItem("Stacked", 1);
    channelMode.addItem("Combined", 2);
    channelMode.setSelectedId(1, juce::dontSendNotification);
    channelMode.onChange = [this] { updateStyle(); };

    gain.setRange(-24.0, 12.0, 0.1);
    gain.setValue(0.0, juce::dontSendNotification);
    fadeIn.setRange(0.0, 600.0, 0.1);
    fadeIn.setSkewFactorFromMidPoint(10.0);
    fadeOut.setRange(0.0, 600.0, 0.1);
    fadeOut.setSkewFactorFromMidPoint(10.0);
    displayScale.setRange(0.25, 64.0, 0.01);
    displayScale.setSkewFactorFromMidPoint(2.0);
    displayScale.setValue(1.0, juce::dontSendNotification);
    for (auto* slider : { &gain, &fadeIn, &fadeOut, &displayScale })
    {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        slider->onValueChange = [this] { updateStyle(); };
    }
    for (auto* label : { &gainLabel, &fadeInLabel, &fadeOutLabel, &scaleLabel })
        label->setJustificationType(juce::Justification::centredRight);

    hint.setText("Drop audio files here. Cmd/Ctrl + wheel or pinch: zoom at pointer. Option/Alt + wheel, T/R, Cmd/Ctrl + ]/[: zoom at centre. Option/Alt + A: zoom to fit. Shift + wheel or swipe: scroll. Option/Alt + Shift + wheel, Cmd + Option/Ctrl + Alt + ]/[: waveform zoom.", juce::dontSendNotification);
    hint.setMinimumHorizontalScale(0.6f);

    for (auto* component : std::initializer_list<juce::Component*>{ &openButton, &syntheticButton, &slowBuild, &channelMode, &gain, &fadeIn, &fadeOut, &displayScale, &gainLabel, &fadeInLabel, &fadeOutLabel, &scaleLabel, &mainView, overview.get(), &hint, &status })
        addAndMakeVisible(component);

    setSize(1200, 640);
    openSynthetic();
    startTimer(100);
}

WaveformDemo::~WaveformDemo() = default;

bool WaveformDemo::isIdle() const
{
    return mainView.getState() != jwaveform::PeakState::loading && !cache->isBuilding() && !(mapping == jtimeline::TimeMapping{});
}

double WaveformDemo::sourceLength() const
{
    const auto data = mainView.getData();
    return data != nullptr ? static_cast<double>(data->getLength()) : 0.0;
}

double WaveformDemo::sampleRate() const
{
    const auto data = mainView.getData();
    return data != nullptr && data->getSampleRate() > 0.0 ? data->getSampleRate() : 48000.0;
}

jtimeline::ZoomLimits WaveformDemo::zoomLimits() const
{
    const auto data = mainView.getData();
    const auto samplesPerPeak = data != nullptr ? data->getLevels().getSamplesPerPeak() : jwaveform::defaultSamplesPerPeak;
    const auto physicalScale = std::max(1.0, static_cast<double>(juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale));
    const auto length = static_cast<jtimeline::SamplePosition>(sourceLength());
    const auto width = static_cast<double>(mainView.getWidth());
    return jtimeline::ZoomLimits::forTimeline(sampleRate(), length, width, samplesPerPeak / physicalScale);
}

void WaveformDemo::openFile(const juce::File& file)
{
    const auto& path = file.getFullPathName();
    auto* manager = &formats;
    setSource({ .key = path + "|" + juce::String{ file.getLastModificationTime().toMilliseconds() },
        .createReader = [manager, path] { return std::unique_ptr<juce::AudioFormatReader>{ manager->createReaderFor(juce::File{ path }) }; },
        .peaks = nullptr });
    getTopLevelComponent()->setName(file.getFileName());
}

void WaveformDemo::openSynthetic()
{
    const auto slow = slowBuild.getToggleState();
    const auto key = "synthetic " + juce::String{ ++syntheticCount } + (slow ? " slow" : "");
    setSource({ .key = key, .createReader = [slow] { return std::make_unique<SyntheticReader>(48000.0, 48000LL * 3600, slow); }, .peaks = nullptr });
}

void WaveformDemo::setSource(const jwaveform::WaveformSource& source)
{
    mainView.setSource(source);
    overview->getView().setSource(source);
    mapping = {};
    updateStyle();
}

void WaveformDemo::setMapping(const jtimeline::TimeMapping& newMapping)
{
    mapping = newMapping;
    const auto start = static_cast<double>(mapping.getStartSample()) + mapping.getStartFraction();
    mainView.setVisibleRange(start, start + (static_cast<double>(mainView.getWidth()) * mapping.getSamplesPerPixel()));
    overview->repaint();
}

void WaveformDemo::centreOn(double sourcePosition)
{
    const auto sample = static_cast<jtimeline::SamplePosition>(sourcePosition);
    const auto centre = mainView.getWidth() / 2.0;
    setMapping(mapping.withSampleAt(sample, centre));
}

void WaveformDemo::updateStyle()
{
    const auto rate = sampleRate();
    const auto length = static_cast<jtimeline::SamplePosition>(sourceLength());
    jwaveform::WaveformStyle style;
    style.channelMode = channelMode.getSelectedId() == 2 ? jwaveform::ChannelMode::combined : jwaveform::ChannelMode::stacked;
    style.displayScale = displayScale.getValue();
    style.envelope.gainDb = gain.getValue();
    style.envelope.fadeIn = { .start = 0, .end = static_cast<jtimeline::SamplePosition>(fadeIn.getValue() * rate) };
    style.envelope.fadeOut = { .start = length - static_cast<jtimeline::SamplePosition>(fadeOut.getValue() * rate), .end = length };
    mainView.setStyle(style);
    auto overviewStyle = style;
    overviewStyle.displayScale = 1.0;
    overview->getView().setStyle(overviewStyle);
}

void WaveformDemo::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto bar = area.removeFromTop(toolbarHeight);
    openButton.setBounds(bar.removeFromLeft(110));
    bar.removeFromLeft(6);
    syntheticButton.setBounds(bar.removeFromLeft(110));
    bar.removeFromLeft(6);
    slowBuild.setBounds(bar.removeFromLeft(100));
    channelMode.setBounds(bar.removeFromLeft(110));
    const auto sliderWidth = bar.getWidth() / 4;
    for (auto [label, slider] : { std::pair{ &gainLabel, &gain }, std::pair{ &fadeInLabel, &fadeIn }, std::pair{ &fadeOutLabel, &fadeOut }, std::pair{ &scaleLabel, &displayScale } })
    {
        auto cell = bar.removeFromLeft(sliderWidth);
        label->setBounds(cell.removeFromLeft(90));
        slider->setBounds(cell);
    }

    area.removeFromTop(6);
    status.setBounds(area.removeFromBottom(22));
    hint.setBounds(area.removeFromBottom(22));
    overview->setBounds(area.removeFromBottom(overviewHeight));
    area.removeFromBottom(6);
    mainView.setBounds(area);
    setMapping(mapping);
}

void WaveformDemo::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto local = event.getEventRelativeTo(&mainView);
    const auto action = jtimeline::wheel::actionFor(jtimeline::wheelModifiersFrom(event.mods), jtimeline::wheelDeltaFrom(wheel));
    const auto limits = zoomLimits();
    auto next = mapping.scrolledBy(action.scrollTimePixels - action.scrollTracksPixels);
    if (action.zoomAtPointerSteps != 0.0)
        next = next.zoomedAround(limits.clamp(jtimeline::wheel::zoomedSamplesPerPixel(next.getSamplesPerPixel(), action.zoomAtPointerSteps, jtimeline::wheel::zoomFactorPerNotch)), static_cast<double>(local.position.x));
    if (action.zoomAtEditCursorSteps != 0.0)
        next = next.zoomedAround(limits.clamp(jtimeline::wheel::zoomedSamplesPerPixel(next.getSamplesPerPixel(), action.zoomAtEditCursorSteps, jtimeline::wheel::zoomFactorPerNotch)), mainView.getWidth() / 2.0);
    if (action.waveformZoomSteps != 0.0)
        displayScale.setValue(displayScale.getValue() * std::pow(jtimeline::wheel::waveformFactorPerStep, action.waveformZoomSteps));
    setMapping(next);
}

void WaveformDemo::mouseDown(const juce::MouseEvent& /*event*/)
{
    grabKeyboardFocus();
}

bool WaveformDemo::keyPressed(const juce::KeyPress& key)
{
    switch (jtimeline::keyActionFor(key))
    {
        case jtimeline::KeyAction::zoomIn:
            zoomAtCentre(1.0, jtimeline::wheel::zoomFactorPerKey);
            return true;
        case jtimeline::KeyAction::zoomOut:
            zoomAtCentre(-1.0, jtimeline::wheel::zoomFactorPerKey);
            return true;
        case jtimeline::KeyAction::zoomToFit:
            zoomToFit();
            return true;
        case jtimeline::KeyAction::waveformTaller:
            displayScale.setValue(displayScale.getValue() * jtimeline::wheel::waveformFactorPerStep);
            return true;
        case jtimeline::KeyAction::waveformShorter:
            displayScale.setValue(displayScale.getValue() / jtimeline::wheel::waveformFactorPerStep);
            return true;
        case jtimeline::KeyAction::none:
        case jtimeline::KeyAction::trackTaller:
        case jtimeline::KeyAction::trackShorter:
        case jtimeline::KeyAction::selectAll:
            break;
    }
    return false;
}

void WaveformDemo::zoomToFit()
{
    setMapping({ zoomLimits().maxSamplesPerPixel, 0 });
}

void WaveformDemo::zoomAtCentre(double steps, double factorPerStep)
{
    setMapping(mapping.zoomedAround(zoomLimits().clamp(jtimeline::wheel::zoomedSamplesPerPixel(mapping.getSamplesPerPixel(), steps, factorPerStep)), mainView.getWidth() / 2.0));
}

void WaveformDemo::mouseMagnify(const juce::MouseEvent& event, float scaleFactor)
{
    if (scaleFactor > 0.0f)
        setMapping(mapping.zoomedAround(zoomLimits().clamp(mapping.getSamplesPerPixel() / static_cast<double>(scaleFactor)), static_cast<double>(event.getEventRelativeTo(&mainView).position.x)));
}

bool WaveformDemo::isInterestedInFileDrag(const juce::StringArray& /*files*/)
{
    return true;
}

void WaveformDemo::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
{
    if (!files.isEmpty())
        openFile(juce::File{ files[0] });
}

void WaveformDemo::timerCallback()
{
    const auto data = mainView.getData();
    if (data != nullptr && mapping == jtimeline::TimeMapping{} && data->getLength() > 0 && mainView.getWidth() > 0)
    {
        zoomToFit();
        updateStyle();
        grabKeyboardFocus();
    }

    juce::String text;
    switch (mainView.getState())
    {
        case jwaveform::PeakState::none:
            text = "No source";
            break;
        case jwaveform::PeakState::loading:
            text = "Building";
            break;
        case jwaveform::PeakState::ready:
            text = "Ready";
            break;
        case jwaveform::PeakState::cannotOpen:
            text = "Can't open";
            break;
        case jwaveform::PeakState::readError:
            text = "Read error";
            break;
    }
    if (data != nullptr)
    {
        const auto physical = mapping.getSamplesPerPixel() / std::max(1.0, static_cast<double>(juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale));
        text << juce::String::formatted(" %.1f%%", 100.0 * static_cast<double>(data->getFramesAppended()) / std::max(1.0, static_cast<double>(data->getLength())))
             << ", " << data->getNumChannels() << " ch at " << data->getSampleRate() << " Hz"
             << ", " << juce::String{ mapping.getSamplesPerPixel(), 2 } << " samples/px, level " << data->getLevels().levelFor(physical)
             << ", peaks " << juce::String{ static_cast<double>(data->getMemoryBytes()) / 1048576.0, 1 } << " MB"
             << ", cache " << juce::String{ static_cast<double>(cache->getMemoryUsage()) / 1048576.0, 1 } << " MB in " << static_cast<int>(cache->getNumEntries()) << " sources";
    }
    status.setText(text, juce::dontSendNotification);
}

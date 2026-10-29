#include "TimelineDemo.h"

TimelineDemo::TimelineDemo()
{
    scenario.addItemList({ "Session: 16 tracks, 1 h", "Stress: 100 tracks, 20,000 clips", "Short: 8 tracks, 2 s", "Empty: 4 tracks, no clips" }, 1);
    scenario.setSelectedId(1, juce::dontSendNotification);
    scenario.onChange = [this] { selectScenario(); };
    format.addItemList({ "h:mm:ss.mmm", "Samples" }, 1);
    format.setSelectedId(1, juce::dontSendNotification);
    format.onChange = [this] { selectFormat(); };

    fit.onClick = [this] { timeline.zoomToFit(); };
    play.setClickingTogglesState(true);
    play.onClick = [this] {
        play.setButtonText(play.getToggleState() ? "Stop" : "Play");
        playStartTime = juce::Time::getMillisecondCounterHiRes();
        playStartPosition = timeline.getPlayhead();
        if (play.getToggleState())
            startTimerHz(60);
        else
            stopTimer();
    };
    follow.onClick = [this] { timeline.setFollowPlayhead(follow.getToggleState()); };
    taller.onClick = [this] {
        const auto ids = timeline.getTrackIds();
        timeline.stepTrackHeights(ids, 1);
    };
    shorter.onClick = [this] {
        const auto ids = timeline.getTrackIds();
        timeline.stepTrackHeights(ids, -1);
    };
    edit.onClick = [this] {
        model.editRandomClips(20);
        updateStatus();
    };
    addTrack.onClick = [this] {
        model.addTrack();
        updateStatus();
    };
    removeTrack.onClick = [this] {
        model.removeTrack();
        updateStatus();
    };

    hint.setText("Click the ruler or overview: playhead / move. Wheel: tracks. Shift: time. Option/Alt + wheel, T/R, Cmd/Ctrl + ]/[: zoom at playhead. "
                 "Cmd/Ctrl + wheel or pinch: zoom at pointer. Option/Alt + A: fit. Home/End: ends.",
        juce::dontSendNotification);
    hint.setMinimumHorizontalScale(0.6f);

    timeline.setModel(model.bind());
    overview.setModel(model.bind());
    overview.setOverviewMode(true);
    timeline.onViewChanged = [this] {
        overview.setHighlightedRange(timeline.getVisibleRange());
        updateStatus();
    };
    timeline.onWaveformScaleChanged = [this] { updateStatus(); };
    timeline.onTimeClicked = [this](jtimeline::SamplePosition position) {
        timeline.setPlayhead(position);
        overview.setPlayhead(position);
        playStartTime = juce::Time::getMillisecondCounterHiRes();
        playStartPosition = position;
        updateStatus();
    };
    overview.onTimeClicked = [this](jtimeline::SamplePosition position) {
        timeline.setMapping(timeline.getMapping().withSampleAt(position, static_cast<double>(timeline.getClipAreaBounds().getWidth()) / 2.0));
    };

    for (auto* component : std::initializer_list<juce::Component*>{ &scenario, &format, &fit, &play, &follow, &taller, &shorter, &edit, &addTrack, &removeTrack, &hint, &status, &timeline, &overview })
        addAndMakeVisible(component);
    setSize(1200, 760);
    updateStatus();
}

TimelineDemo::~TimelineDemo()
{
    stopTimer();
    timeline.setModel({});
    overview.setModel({});
}

void TimelineDemo::selectScenario()
{
    play.setToggleState(false, juce::sendNotification);
    model.generate(static_cast<DemoModel::Scenario>(scenario.getSelectedId() - 1));
    timeline.setPlayhead(0);
    overview.setPlayhead(0);
    timeline.zoomToFit();
    updateStatus();
}

void TimelineDemo::selectFormat()
{
    if (format.getSelectedId() == 2)
        timeline.setRulerFormatter([](double position, double, double) { return juce::String{ static_cast<juce::int64>(std::llround(position)) }; });
    else
        timeline.setRulerFormatter({});
}

void TimelineDemo::timerCallback()
{
    const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - playStartTime) / 1000.0;
    const auto position = playStartPosition + static_cast<jtimeline::SamplePosition>(elapsed * DemoModel::getSampleRate());
    if (position >= model.getLength())
    {
        play.setToggleState(false, juce::sendNotification);
        return;
    }
    timeline.setPlayhead(position);
    overview.setPlayhead(position);
    updateStatus();
}

void TimelineDemo::updateStatus()
{
    const auto& mapping = timeline.getMapping();
    const auto visible = timeline.getVisibleRange();
    const auto rate = DemoModel::getSampleRate();
    status.setText(juce::String{ model.getNumTracks() } + " tracks, " + juce::String{ static_cast<juce::int64>(model.getNumClips()) } + " clips. "
            + juce::String{ mapping.getSamplesPerPixel(), 4 } + " samples/px, visible " + jtimeline::ruler::formatTime(static_cast<double>(visible.start) / rate, 0.001)
            + " to " + jtimeline::ruler::formatTime(static_cast<double>(visible.end) / rate, 0.001) + ", playhead "
            + jtimeline::ruler::formatTime(static_cast<double>(timeline.getPlayhead()) / rate, 0.001) + ", waveform " + juce::String{ timeline.getWaveformScale(), 2 } + "x",
        juce::dontSendNotification);
}

void TimelineDemo::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto top = area.removeFromTop(28);
    scenario.setBounds(top.removeFromLeft(230));
    top.removeFromLeft(6);
    format.setBounds(top.removeFromLeft(120));
    for (const auto& [button, width] : std::initializer_list<std::pair<juce::Component*, int>>{ { &fit, 100 }, { &play, 70 }, { &follow, 110 }, { &taller, 70 }, { &shorter, 70 }, { &edit, 100 }, { &addTrack, 90 }, { &removeTrack, 100 } })
    {
        top.removeFromLeft(6);
        button->setBounds(top.removeFromLeft(width));
    }
    area.removeFromTop(4);
    hint.setBounds(area.removeFromTop(22));
    status.setBounds(area.removeFromBottom(24));
    overview.setBounds(area.removeFromBottom(80));
    area.removeFromBottom(6);
    timeline.setBounds(area);
}

void TimelineDemo::applyArguments(const juce::StringArray& args)
{
    if (const auto index = args.indexOf("--scenario"); index >= 0 && index + 1 < args.size())
        scenario.setSelectedId(args[index + 1].getIntValue(), juce::sendNotificationSync);
    if (const auto index = args.indexOf("--visible"); index >= 0 && index + 2 < args.size())
    {
        const auto rate = DemoModel::getSampleRate();
        timeline.setVisibleRange({ .start = static_cast<jtimeline::SamplePosition>(args[index + 1].getDoubleValue() * rate), .end = static_cast<jtimeline::SamplePosition>(args[index + 2].getDoubleValue() * rate) });
    }
}

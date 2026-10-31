#include "TimelineDemo.h"

namespace
{

std::shared_ptr<jwaveform::PeakCache> makeCache()
{
    jwaveform::PeakCache::Options options;
    options.memoryLimitBytes = std::size_t{ 512 } * 1024 * 1024;
    return std::make_shared<jwaveform::PeakCache>(options);
}

}

TimelineDemo::TimelineDemo() :
    cache(makeCache())
{
    scenario.addItemList({ "Session: 16 tracks + status examples, 1 h", "Stress: 100 tracks, 20,000 clips", "Short: 8 tracks, 2 s", "Empty: 4 tracks, no clips" }, 1);
    scenario.setSelectedId(1, juce::dontSendNotification);
    scenario.onChange = [this] { selectScenario(); };
    format.addItemList({ "h:mm:ss.mmm", "Samples" }, 1);
    format.setSelectedId(1, juce::dontSendNotification);
    format.onChange = [this] { selectFormat(); };

    fit.onClick = [this] { timeline.zoomToFit(); };
    zoomSelection.onClick = [this] { timeline.zoomToSelection(); };
    play.setClickingTogglesState(true);
    play.onClick = [this] {
        play.setButtonText(play.getToggleState() ? "Stop" : "Play");
        playStartTime = juce::Time::getMillisecondCounterHiRes();
        playStartPosition = timeline.getPlayhead();
        startTimerHz(play.getToggleState() ? 60 : 4);
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
    addFilesButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Add audio files", juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        const auto modes = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems; // NOLINT(bugprone-signed-bitwise)
        chooser->launchAsync(modes, [this](const juce::FileChooser& fc) { addFiles(fc.getResults()); });
    };
    slowBuilds.onClick = [this] {
        model.setSlowBuilds(slowBuilds.getToggleState());
        updateStatus();
    };

    hint.setText("Drop audio files to add tracks. Click the ruler, a marker or the overview: playhead / move. Drag the ruler: time selection. Wheel: tracks. Shift: time. Option/Alt + wheel, T/R, Cmd/Ctrl + ]/[: "
                 "zoom at selected clips, else edit cursor. Cmd/Ctrl + wheel or pinch: zoom at pointer. Option/Alt + A: fit. Option/Alt + F: zoom to selection. Home/End: ends. Space: play/stop.\n"
                 "Click clips or headers to select (Cmd/Ctrl toggles, Shift: range from cursor); drag empty space or Shift + drag: rubber band; Cmd/Ctrl + A: all. Arrows, Shift + arrows, Return, Esc: keyboard cursor. "
                 "Ctrl + Up/Down: track height. Option/Alt + Shift + wheel, Cmd + Option/Ctrl + Alt + ]/[: waveform height.",
        juce::dontSendNotification);
    hint.setFont(juce::FontOptions{ 13.0f });
    hint.setMinimumHorizontalScale(0.6f);
    hint.setJustificationType(juce::Justification::topLeft);

    timeline.setPeakCache(cache);
    overview.setPeakCache(cache);
    timeline.setModel(model.bind());
    overview.setModel(model.bind());
    overview.setOverviewMode(true);
    timeline.onViewChanged = [this] {
        overview.setHighlightedRange(timeline.getVisibleRange());
        updateStatus();
    };
    timeline.onWaveformScaleChanged = [this] { updateStatus(); };
    timeline.onSelectionChanged = [this] { updateStatus(); };
    timeline.onPlayStop = [this] { play.triggerClick(); };
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

    for (auto* component : std::initializer_list<juce::Component*>{ &scenario, &format, &fit, &zoomSelection, &play, &follow, &taller, &shorter, &edit, &addTrack, &removeTrack, &addFilesButton, &slowBuilds, &hint, &status, &selectionStatus, &timeline, &overview })
        addAndMakeVisible(component);
    setSize(1200, 840);
    updateStatus();
    startTimer(250);
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

bool TimelineDemo::isIdle() const
{
    return !cache->isBuilding() && !cache->isFetchingSamples();
}

bool TimelineDemo::isInterestedInFileDrag(const juce::StringArray& /*files*/)
{
    return true;
}

void TimelineDemo::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
{
    juce::Array<juce::File> list;
    for (const auto& path : files)
        list.add(juce::File{ path });
    addFiles(list);
}

void TimelineDemo::addFiles(const juce::Array<juce::File>& files)
{
    if (model.addFiles(files) > 0)
        timeline.setTrackScroll(timeline.getTrackScroll() + 1'000'000);
    updateStatus();
}

void TimelineDemo::timerCallback()
{
    if (!play.getToggleState())
    {
        updateStatus();
        return;
    }
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
            + jtimeline::ruler::formatTime(static_cast<double>(timeline.getPlayhead()) / rate, 0.001) + ", waveform " + juce::String{ timeline.getWaveformScale(), 2 } + "x. Peaks "
            + juce::String{ static_cast<double>(cache->getMemoryUsage()) / (1024.0 * 1024.0), 1 } + " MB" + (cache->isBuilding() ? ", building" : ""),
        juce::dontSendNotification);

    const auto time = [rate](jtimeline::SamplePosition position) { return jtimeline::ruler::formatTime(static_cast<double>(position) / rate, 0.001); };
    const auto& selection = timeline.getSelection();
    auto text = juce::String{ "Selected: " } + juce::String{ static_cast<juce::int64>(selection.getClips().size()) } + " clips, " + juce::String{ static_cast<juce::int64>(selection.getTracks().size()) } + " tracks";
    if (const auto range = timeline.getTimeSelection())
        text << ", time " << time(range->start) << " to " << time(range->end);
    const auto& keyboardCursor = timeline.getKeyboardCursor();
    if (keyboardCursor.clip)
        text << ". Cursor on clip " << juce::String{ static_cast<juce::int64>(*keyboardCursor.clip) };
    else if (keyboardCursor.track)
        text << ". Cursor on track " << juce::String{ static_cast<juce::int64>(*keyboardCursor.track) };
    if (const auto range = timeline.getTimelineSelection())
        text << ". Timeline selection " << time(range->start) << " to " << time(range->end);
    selectionStatus.setText(text, juce::dontSendNotification);
}

void TimelineDemo::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto top = area.removeFromTop(28);
    scenario.setBounds(top.removeFromLeft(260));
    top.removeFromLeft(6);
    format.setBounds(top.removeFromLeft(120));
    for (const auto& [button, width] : std::initializer_list<std::pair<juce::Component*, int>>{ { &addFilesButton, 100 }, { &slowBuilds, 110 }, { &edit, 100 }, { &addTrack, 90 }, { &removeTrack, 100 } })
    {
        top.removeFromLeft(6);
        button->setBounds(top.removeFromLeft(width));
    }
    area.removeFromTop(4);
    auto second = area.removeFromTop(28);
    for (const auto& [button, width] : std::initializer_list<std::pair<juce::Component*, int>>{ { &fit, 100 }, { &zoomSelection, 130 }, { &play, 70 }, { &follow, 110 }, { &taller, 70 }, { &shorter, 70 } })
    {
        button->setBounds(second.removeFromLeft(width));
        second.removeFromLeft(6);
    }
    area.removeFromTop(4);
    hint.setBounds(area.removeFromTop(52));
    selectionStatus.setBounds(area.removeFromBottom(22));
    status.setBounds(area.removeFromBottom(22));
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
    if (const auto index = args.indexOf("--select"); index >= 0 && index + 2 < args.size())
    {
        const auto rate = DemoModel::getSampleRate();
        const jtimeline::SampleRange range{ .start = static_cast<jtimeline::SamplePosition>(args[index + 1].getDoubleValue() * rate), .end = static_cast<jtimeline::SamplePosition>(args[index + 2].getDoubleValue() * rate) };
        timeline.setTimeSelection(range);
        timeline.setSelectedClips(model.clipsOnTracks(range, 2, 4));
        if (const auto ids = timeline.getTrackIds(); ids.size() > 6)
            timeline.setSelectedTracks(std::vector{ ids[6] });
        timeline.grabKeyboardFocus();
    }
}

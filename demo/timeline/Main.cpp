#include "MappingView.h"

namespace
{

class MainComponent final : public juce::Component
{
public:
    MainComponent()
    {
        fit.onClick = [this] { view.zoomToFit(); };
        hint.setText("Click: playhead. Wheel: tracks. Shift: time. Option/Alt + wheel, T/R, Cmd/Ctrl + ]/[: zoom at playhead. Cmd/Ctrl + wheel: zoom at pointer. Option/Alt + A: fit. Option/Alt + Shift + wheel, Cmd + Option + ]/[: waveform height.", juce::dontSendNotification);
        hint.setMinimumHorizontalScale(0.7f);
        for (auto* component : std::initializer_list<juce::Component*>{ &fit, &hint, &status, &view })
            addAndMakeVisible(component);
        setSize(1100, 600);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto top = area.removeFromTop(28);
        fit.setBounds(top.removeFromLeft(100));
        top.removeFromLeft(8);
        hint.setBounds(top);
        status.setBounds(area.removeFromBottom(24));
        view.setBounds(area);
    }

private:
    void updateStatus()
    {
        const auto& mapping = view.getMapping();
        const auto visible = mapping.visibleRange(view.getWidth());
        status.setText(juce::String{ mapping.getSamplesPerPixel(), 4 } + " samples/px, start "
                + juce::String{ mapping.getStartSample() } + " + " + juce::String{ mapping.getStartFraction(), 3 }
                + ", visible " + juce::String{ visible.start } + " to " + juce::String{ visible.end }
                + ", playhead " + juce::String{ view.getPlayhead() } + ", waveform " + juce::String{ view.getWaveformScale(), 2 } + "x",
            juce::dontSendNotification);
    }

    MappingView view{ [this] { updateStatus(); } };
    juce::TextButton fit{ "Zoom to fit" };
    juce::Label hint;
    juce::Label status;
};

class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow(const juce::String& name) :
        DocumentWindow(name, juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId), allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent, true);
        setResizable(true, true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class DemoApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    void initialise(const juce::String& /*commandLine*/) override { window = std::make_unique<MainWindow>(getApplicationName()); }
    void shutdown() override { window.reset(); }

private:
    std::unique_ptr<MainWindow> window;
};

}

START_JUCE_APPLICATION(DemoApplication)

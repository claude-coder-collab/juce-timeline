#include "MappingView.h"

namespace
{

class MainComponent final : public juce::Component
{
public:
    MainComponent()
    {
        fit.onClick = [this] { view.zoomToFit(); };
        hint.setText("Cmd/Ctrl + wheel or pinch zooms at the pointer; wheel scrolls time.", juce::dontSendNotification);
        for (auto* component : std::initializer_list<juce::Component*>{ &fit, &hint, &status, &view })
            addAndMakeVisible(component);
        setSize(1000, 300);
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
                + ", visible " + juce::String{ visible.start } + " to " + juce::String{ visible.end },
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

#include "TimelineDemo.h"

namespace
{

class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow(const juce::String& name) :
        DocumentWindow(name, juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId), allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new TimelineDemo, true);
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
    void initialise(const juce::String& commandLine) override
    {
        window = std::make_unique<MainWindow>(getApplicationName());
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        if (auto* demo = dynamic_cast<TimelineDemo*>(window->getContentComponent()))
            demo->applyArguments(args);
        const auto index = args.indexOf("--snapshot");
        if (index >= 0 && index + 1 < args.size())
            snapshot = std::make_unique<Snapshot>(*window, juce::File{ args[index + 1].unquoted() });
    }

    void shutdown() override
    {
        snapshot.reset();
        window.reset();
    }

private:
    class Snapshot final : private juce::Timer
    {
    public:
        Snapshot(juce::DocumentWindow& windowIn, juce::File fileIn) :
            window(windowIn),
            file(std::move(fileIn))
        {
            startTimer(500);
        }

    private:
        void timerCallback() override
        {
            auto* content = window.getContentComponent();
            const auto* demo = dynamic_cast<TimelineDemo*>(content);
            if (demo != nullptr && !demo->isIdle() && ++waits < 240)
                return;
            stopTimer();
            file.deleteFile();
            juce::FileOutputStream stream{ file };
            juce::PNGImageFormat{}.writeImageToStream(content->createComponentSnapshot(content->getLocalBounds(), true, 2.0f), stream);
            stream.flush();
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        juce::DocumentWindow& window;
        juce::File file;
        int waits = 0;
    };

    std::unique_ptr<MainWindow> window;
    std::unique_ptr<Snapshot> snapshot;
};

}

START_JUCE_APPLICATION(DemoApplication)

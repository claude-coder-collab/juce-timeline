#include <jtimeline/jtimeline.h>

int main()
{
    const juce::ScopedJuceInitialiser_GUI juce;
    const jtimeline::TimeMapping mapping{ 2.0, 100 };
    const jwaveform::PeakLevels levels{ 48000 };
    return mapping.sampleAt(10.0) == 120 && levels.getLevelCount() > 1 ? 0 : 1;
}

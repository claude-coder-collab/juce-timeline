#include "jtimeline_WheelInput.h"

#include <cmath>

namespace jtimeline::wheel
{

namespace
{

double steps(double delta, bool isSmooth)
{
    if (isSmooth)
        return delta / smoothDeltaPerNotch;
    if (delta == 0.0)
        return 0.0;
    return delta > 0.0 ? 1.0 : -1.0;
}

double pixels(double delta, bool isSmooth)
{
    return isSmooth ? delta * pixelsPerSmoothDelta : steps(delta, false) * pixelsPerNotch;
}

double eitherDelta(WheelDelta delta)
{
    return delta.deltaY != 0.0 ? delta.deltaY : delta.deltaX;
}

}

WheelAction actionFor(WheelModifiers modifiers, WheelDelta delta)
{
    const auto [shift, alt, command, other] = modifiers;
    WheelAction action;
    if (other)
        return action;

    if (!shift && !alt && !command)
    {
        action.scrollTracksPixels = -pixels(delta.deltaY, delta.isSmooth);
        action.scrollTimePixels = -pixels(delta.deltaX, delta.isSmooth);
    }
    else if (shift && !alt && !command)
    {
        action.scrollTimePixels = -pixels(eitherDelta(delta), delta.isSmooth);
    }
    else if (alt && !shift && !command)
    {
        action.zoomAtAnchorSteps = steps(delta.deltaY, delta.isSmooth);
    }
    else if (command && !shift && !alt)
    {
        action.zoomAtPointerSteps = steps(delta.deltaY, delta.isSmooth);
    }
    else if (alt && shift && !command)
    {
        action.waveformZoomSteps = -steps(eitherDelta(delta), delta.isSmooth);
    }
    return action;
}

double zoomedSamplesPerPixel(double samplesPerPixel, double steps, double factorPerStep)
{
    return samplesPerPixel * std::pow(factorPerStep, -steps);
}

}

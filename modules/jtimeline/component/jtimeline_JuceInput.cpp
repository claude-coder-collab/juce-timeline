#include "jtimeline_JuceInput.h"

namespace jtimeline
{

bool isMacPlatform()
{
    return (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX) != 0;
}

WheelModifiers wheelModifiersFrom(const juce::ModifierKeys& modifiers)
{
    return { .shift = modifiers.isShiftDown(), .alt = modifiers.isAltDown(), .command = modifiers.isCommandDown(), .other = isMacPlatform() && modifiers.isCtrlDown() };
}

WheelDelta wheelDeltaFrom(const juce::MouseWheelDetails& wheel)
{
    return { .deltaX = static_cast<double>(wheel.deltaX), .deltaY = static_cast<double>(wheel.deltaY), .isSmooth = wheel.isSmooth };
}

KeyInput keyInputFrom(const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    KeyInput input{ .character = 0, .named = KeyInput::Named::none, .shift = modifiers.isShiftDown(), .alt = modifiers.isAltDown(), .command = modifiers.isCommandDown(), .control = modifiers.isCtrlDown() };
    const auto code = key.getKeyCode();
    if (code == juce::KeyPress::upKey)
        input.named = KeyInput::Named::up;
    else if (code == juce::KeyPress::downKey)
        input.named = KeyInput::Named::down;
    else if (code == juce::KeyPress::homeKey)
        input.named = KeyInput::Named::home;
    else if (code == juce::KeyPress::endKey)
        input.named = KeyInput::Named::end;
    else if (code > 0)
        input.character = static_cast<char32_t>(code);
    return input;
}

KeyAction keyActionFor(const juce::KeyPress& key)
{
    return keys::actionFor(keyInputFrom(key), isMacPlatform());
}

}

#pragma once

#include "../core/jtimeline_KeyInput.h"
#include "../core/jtimeline_Selection.h"
#include "../core/jtimeline_WheelInput.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jtimeline
{

/** Returns true when running on macOS, where Control is separate from Command. */
[[nodiscard]] bool isMacPlatform();

/** Converts JUCE modifier keys for wheel bindings. */
[[nodiscard]] WheelModifiers wheelModifiersFrom(const juce::ModifierKeys& modifiers);

/** Converts a JUCE wheel event's deltas. */
[[nodiscard]] WheelDelta wheelDeltaFrom(const juce::MouseWheelDetails& wheel);

/** Converts a JUCE key press for key bindings. */
[[nodiscard]] KeyInput keyInputFrom(const juce::KeyPress& key);

/** Returns the action for a JUCE key press on this platform. */
[[nodiscard]] KeyAction keyActionFor(const juce::KeyPress& key);

/** Returns how a click with these modifiers selects: Shift adds, Command/Ctrl toggles, otherwise replaces. */
[[nodiscard]] SelectMode selectModeFrom(const juce::ModifierKeys& modifiers);

}

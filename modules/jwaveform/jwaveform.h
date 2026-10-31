/*
BEGIN_JUCE_MODULE_DECLARATION

    ID:               jwaveform
    vendor:           jtimeline
    version:          0.1.0
    name:             JUCE Waveform
    description:      Multi-resolution waveform peaks and drawing for audio sources.
    minimumCppStandard: 23
    dependencies:     juce_audio_formats juce_gui_basics

END_JUCE_MODULE_DECLARATION
*/

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/jwaveform_PeakBuilder.h"
#include "core/jwaveform_PeakCache.h"
#include "core/jwaveform_PeakColumns.h"
#include "core/jwaveform_PeakData.h"
#include "core/jwaveform_PeakLevels.h"
#include "core/jwaveform_RawSamples.h"
#include "core/jwaveform_SampleRange.h"
#include "view/jwaveform_WaveformDrawing.h"
#include "view/jwaveform_WaveformView.h"

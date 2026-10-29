/*
BEGIN_JUCE_MODULE_DECLARATION

    ID:               jtimeline
    vendor:           jtimeline
    version:          0.1.0
    name:             JUCE Timeline
    description:      A timeline view of tracks and clips with waveform previews, bound to a user-supplied model.
    minimumCppStandard: 23
    dependencies:     jwaveform juce_gui_basics

END_JUCE_MODULE_DECLARATION
*/

#pragma once

#include <jwaveform/jwaveform.h>

#include "component/jtimeline_JuceInput.h"
#include "component/jtimeline_Timeline.h"
#include "component/jtimeline_TimelineLookAndFeel.h"
#include "component/jtimeline_TrackHeader.h"
#include "core/jtimeline_Change.h"
#include "core/jtimeline_Ids.h"
#include "core/jtimeline_KeyInput.h"
#include "core/jtimeline_LevelOfDetail.h"
#include "core/jtimeline_RulerTicks.h"
#include "core/jtimeline_TimeMapping.h"
#include "core/jtimeline_TimelineModel.h"
#include "core/jtimeline_TimelineNotifier.h"
#include "core/jtimeline_TrackLayout.h"
#include "core/jtimeline_WheelInput.h"

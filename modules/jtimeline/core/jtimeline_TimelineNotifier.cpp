#include "jtimeline_TimelineNotifier.h"

namespace jtimeline
{

TimelineNotifier::~TimelineNotifier() // NOLINT(bugprone-exception-escape)
{
    listeners.call([this](Listener& listener) { listener.notifierDestroyed(*this); });
}

void TimelineNotifier::changed(const Change& change)
{
    listeners.call([&change](Listener& listener) { listener.modelChanged(change); });
}

void TimelineNotifier::addListener(Listener* listener) { listeners.add(listener); }

void TimelineNotifier::removeListener(Listener* listener) { listeners.remove(listener); }

int TimelineNotifier::getNumListeners() const { return listeners.size(); }

}

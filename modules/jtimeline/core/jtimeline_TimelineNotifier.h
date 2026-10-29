#pragma once

#include "jtimeline_Change.h"

#include <juce_core/juce_core.h>

namespace jtimeline
{

//==============================================================================
/**
    Lets a model tell every Timeline viewing it what changed.

    The model owns a notifier and points TimelineModel::notifier at it. Each Timeline bound to the
    model subscribes when bound and unsubscribes when unbound or destroyed, so one call reaches
    every view. If the notifier is destroyed first, its views detach from the model and show as
    empty.

    Use it on the message thread only.
*/
class TimelineNotifier
{
public:
    /** Receives notifications. Timeline implements this; you rarely need to. */
    class Listener
    {
    public:
        Listener() = default;
        virtual ~Listener() = default;
        /** @internal */
        Listener(const Listener&) = default;
        /** @internal */
        Listener& operator=(const Listener&) = default;
        /** @internal */
        Listener(Listener&&) = default;
        /** @internal */
        Listener& operator=(Listener&&) = default;

        /** Called for every change sent with TimelineNotifier::changed(). */
        virtual void modelChanged(const Change& change) = 0;
        /** Called when the notifier is being destroyed. A listener should stop using it. */
        virtual void notifierDestroyed(TimelineNotifier& notifier) = 0;
    };

    /** Creates a notifier with no listeners. */
    TimelineNotifier() = default;
    /** Destructor. Tells every remaining listener that the notifier is going away. */
    ~TimelineNotifier();

    TimelineNotifier(const TimelineNotifier&) = delete;
    TimelineNotifier& operator=(const TimelineNotifier&) = delete;
    TimelineNotifier(TimelineNotifier&&) = delete;
    TimelineNotifier& operator=(TimelineNotifier&&) = delete;

    /** Sends a change to every listener. */
    void changed(const Change& change);
    /** Adds a listener. Timelines do this themselves when bound. */
    void addListener(Listener* listener);
    /** Removes a listener. */
    void removeListener(Listener* listener);
    /** Returns the number of listeners. */
    [[nodiscard]] int getNumListeners() const;

private:
    juce::ListenerList<Listener> listeners;
};

}

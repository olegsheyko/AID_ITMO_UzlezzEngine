#pragma once

#include "events/EventDispatcher.h"

class ServiceLocator {
public:
    static EventDispatcher& getEventDispatcher() {
        static EventDispatcher dispatcher;
        return dispatcher;
    }
};

#include "EventBus.h"

namespace hp {

EventBus& EventBus::instance() {
    static EventBus b;
    return b;
}

bool EventBus::subscribe(EventHandler h) {
    if (!h) return false;

    // Do not register the same handler twice.
    for (size_t i = 0; i < n_; ++i) {
        if (h_[i] == h) return true;
    }

    if (n_ >= MAX) return false;
    h_[n_++] = h;
    return true;
}

void EventBus::publish(const Event& e) {
    // Fixed-capacity bus: no heap allocation while publishing.
    for (size_t i = 0; i < n_; ++i) {
        if (h_[i]) h_[i](e);
    }
}

}

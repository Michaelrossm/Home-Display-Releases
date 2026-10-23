#pragma once
#include <NetworkEvents.h>
namespace hp { class EthernetService { public: static EthernetService& instance(); bool begin(); private: static void onEvent(arduino_event_t* event); bool started_=false; }; }

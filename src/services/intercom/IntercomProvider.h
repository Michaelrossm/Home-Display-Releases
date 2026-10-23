#pragma once
#include <Arduino.h>
namespace hp {
    struct IntercomCapabilities {
        bool ringEvents = false, answer = false, hangup = false, door = false, duplex = false,
             video = false;
    };
    struct IntercomDevice {
        char id[33]{}, owner[65]{}, provider[25]{}, name[65]{}, participant[65]{}, host[97]{},
            username[65]{};
        uint16_t port = 80;
        uint16_t videoPort = 554;
        char videoPath[193]{};
        uint8_t fps = 5;
        bool httpVideo = false;
        bool ringTone = true;
        bool enabled = false;
    };
    enum class IntercomAction : uint8_t { Answer, Hangup, Mute, Door };
    // Firmware-owned providers live for the lifetime of the firmware. Functions are
    // called exclusively by NetworkWorker, never by Lua/LVGL. No command retries.
    class IntercomProvider {
      public:
        virtual ~IntercomProvider() = default;
        virtual const char *id() const = 0;
        virtual IntercomCapabilities capabilities(const IntercomDevice &) const = 0;
        virtual bool command(const IntercomDevice &, IntercomAction, bool, const String &secret,
                             String &error) = 0;
    };
} // namespace hp

#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace hp {

class CalendarAuthService {
public:
    static CalendarAuthService& instance();

    bool setPassword(uint32_t personId, const char* password);
    bool verify(uint32_t personId, const char* password);

private:
    struct Credential {
        uint32_t personId = 0;
        uint8_t salt[16]{};
        uint8_t hash[32]{};
    };

    bool load(uint32_t personId, Credential& out);
    bool save(const Credential& c);
    void derive(const char* password, const uint8_t salt[16], uint8_t out[32]);
};

}

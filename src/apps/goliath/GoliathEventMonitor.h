#pragma once
#include <Arduino.h>
#include <NetworkClient.h>

namespace hp {
// Read-only Dahua event stream. Runs only in the existing NetworkWorker maintenance loop.
// Unverified apartment routing is diagnostic-only; it never opens another apartment's call.
class GoliathEventMonitor {
public:
    static GoliathEventMonitor& instance();
    bool begin();
    void settingsChanged();
    void poll();
    const char* stateName() const;
    uint32_t observedCalls() const { return observed_; }
private:
    GoliathEventMonitor() = default;
    static void maintenance(void* ctx);
    void disconnect(const char* reason, uint32_t backoff = 10000);
    void handleLine(const String& line);
    void sendRequest();
    NetworkClient socket_;
    String headers_, line_, challenge_, realm_, nonce_, opaque_, qop_, user_, password_, apartment_;
    String host_;
    uint16_t port_ = 80;
    uint32_t retryAt_ = 0, lastByte_ = 0, nextConfigAt_ = 0, observed_ = 0;
    uint8_t attempts_ = 0;
    bool registered_ = false, enabled_ = false, headersDone_ = false, authorized_ = false;
    char state_[24] = "disabled";
};
}

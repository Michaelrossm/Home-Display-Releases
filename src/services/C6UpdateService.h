#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>

namespace hp {

class C6UpdateService {
public:
    static C6UpdateService& instance();

    bool begin();
    bool requestOfficialUpdate();
    bool inProgress() const;
    String status() const;

private:
    static void job(void*);
    void run();
    void setStatus(const String& text, bool busy);
    void finish(const String& text);

    SemaphoreHandle_t lock_ = nullptr;
    bool busy_ = false;
    String status_ = "C6-Update bereit.";
};

}

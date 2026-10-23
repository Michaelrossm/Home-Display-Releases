#pragma once
#include <Arduino.h>

struct WeatherDay {
    String date;
    int code = -1;
    float minimum = NAN;
    float maximum = NAN;
    uint8_t rainProbability = 0;
};

struct WeatherData {
    bool online = false;
    String region;
    String location;
    String provider;
    String error;
    float temperature = NAN;
    float apparentTemperature = NAN;
    float windKmh = NAN;
    uint8_t humidity = 0;
    int code = -1;
    bool isDay = true;
    WeatherDay days[4];
    uint32_t updatedAt = 0;
};

class WeatherClient {
public:
    bool update(const String& region, WeatherData& out);
    static const char* conditionDe(int code);
};

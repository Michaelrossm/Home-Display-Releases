#pragma once
#include <Arduino.h>
#include "../../../services/EnergyModel.h"

struct PowerMeterData {
    float voltage[3] = {NAN, NAN, NAN};
    float current[3] = {NAN, NAN, NAN};
    float power[3] = {NAN, NAN, NAN};
    float totalPower = NAN;
    float totalImportKwh = NAN;
    float totalExportKwh = NAN;
    bool online = false;
    bool nativeStatus = false;
    bool countersValid = false;
    uint32_t updatedMs = 0;
    String error;
};

struct PowerMeterHistoryView {
    static constexpr uint16_t HOURLY_CAPACITY = 168;
    static constexpr uint16_t DAILY_CAPACITY = 366;
    float hourlyImportWh[HOURLY_CAPACITY] = {};
    float hourlyExportWh[HOURLY_CAPACITY] = {};
    float dailyImportWh[DAILY_CAPACITY] = {};
    float dailyExportWh[DAILY_CAPACITY] = {};
    uint16_t hourlyCount = 0;
    uint16_t dailyCount = 0;
    float averageImportKwhPerDay = NAN;
    float averageExportKwhPerDay = NAN;
    float currentHourImportWh = 0;
    float currentHourExportWh = 0;
    float currentDayImportWh = 0;
    float currentDayExportWh = 0;
    uint32_t trackingSeconds = 0;
    bool loadedFromTracker = false;
};

class PowerMeterClient {
public:
    bool fetchDetailHistory(const String& host,const String& password,int64_t from,int64_t to,const hp::EnergyHistoryProvider::Visitor& visitor);
    bool poll(const String& host, PowerMeterData& out,const String& password="");
    bool loadHistory(const String& host, const String& adminPassword,
                     PowerMeterHistoryView& out, String& errorOut);

private:
    bool requestJson(const String& url, String& body, String& errorOut,
                     const String& password = "", size_t maxBody = 4096,
                     uint16_t timeoutMs = 650);
    static bool normalizeHost(const String& raw, String& normalized);
    static bool jsonNumber(const String& json, const char* key, float& value);
    static bool firstNumber(const String& json,
                            const char* const* keys, size_t keyCount,
                            float& value);
    static bool taggedObject(const String& json, const char* key,
                             const char* value, String& object);
    static void sanitizeData(PowerMeterData& data);
};

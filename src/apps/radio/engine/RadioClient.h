#pragma once
#include <Arduino.h>

struct RadioMode {
    int key = -1;
    String id;
    String label;
    bool selectable = false;
};

struct RadioNavItem {
    int key = -1;
    int type = 0;
    int subtype = 0;
    String name;
};

struct RadioChoice {
    int key = -1;
    String label;
};

struct RadioPreset {
    int key = -1;
    String name;
};

struct RadioState {
    bool online = false;
    bool power = false;
    bool mute = false;

    int volume = 0;
    int volumeSteps = 20;
    int mode = -1;

    // Frontier player state: 0 stopped/unknown, 1 buffering,
    // 2 playing, 3 paused.
    int playStatus = 0;

    String friendlyName;
    String station;
    String text;
    String artist;
    String album;
    String graphicUri;
    String error;

    // UNDOK-style settings/status
    uint32_t sleepSeconds = 0;
    int eqPreset = 0;
    int eqBass = 0;
    int eqTreble = 0;
    bool eqLoudness = false;

    int clockMode = 1;
    int clockSource = 0;
    int32_t utcOffset = 0;
    int language = 0;

    String localDate;
    String localTime;
    String softwareVersion;
};

class RadioClient {
public:
    void configure(String host, String pin);

    const String& host() const { return _host; }
    const String& pin() const { return _pin; }
    const String& lastError() const { return _lastError; }

    bool ping();

    bool pollCore(RadioState& out);
    bool pollNowPlaying(RadioState& out);

    bool setPower(bool on);
    bool setMute(bool on);
    bool setVolume(int value);
    bool setMode(int modeKey);
    bool selectPreset(int presetKey);
    bool playControl(int command);
    bool getFmFrequency(uint32_t& frequencyKhz);
    bool getFmRange(uint32_t& lowerKhz, uint32_t& upperKhz,
                    uint32_t& stepKhz);
    bool tuneFm(uint32_t frequencyKhz);
    bool getStationName(String& name);

    bool pollSettings(RadioState& out);
    bool setSleep(uint32_t seconds);
    bool setEqPreset(int key);
    bool setEqBass(int value);
    bool setEqTreble(int value);
    bool setEqLoudness(bool on);
    bool setClockMode(int mode);
    bool setClockSource(int source);
    bool setLanguage(int language);

    int loadEqPresets(RadioChoice* target, int maxItems);
    int loadLanguages(RadioChoice* target, int maxItems);
    int loadClockSources(RadioChoice* target, int maxItems);

    bool navBegin();
    bool navRestartForInternet();
    int loadNav(RadioNavItem* target, int maxItems, int offset = -1);
    int loadNavAll(RadioNavItem* target, int maxItems);
    bool navSelect(const RadioNavItem& item);
    bool navBack();
    int searchStations(const String& searchTerm,
                       RadioNavItem* target, int maxItems);

    // Internet-radio station helper: uses the radio's own Frontier/UNDOK
    // catalogue. No external stream URLs are stored in Fitz32-Touch.
    bool searchAndPlayStation(const String& searchTerm);
    int copyLastInternetResults(RadioNavItem* target, int maxItems) const;

    int loadModes(RadioMode* target, int maxItems);
    int loadPresets(RadioPreset* target, int maxItems, int offset = -1);

private:
    String _host;
    String _pin = "1234";
    String _lastError;
    int _lastInternetResultCount = 0;

    bool request(const String& path, String& response, uint16_t timeoutMs = 650,bool replaySafe=true);
    bool apiGet(const String& node, String& response);
    bool apiSet(const String& node, const String& value);
    bool listGetNext(const String& node, String& response, int maxItems, int offset = -1);
    bool waitNavReady(uint32_t timeoutMs = 4000);

    static String urlEncode(const String& value);
    static bool isOk(const String& xml);
    static String xmlValue(const String& xml);
    static String fieldValue(const String& itemXml, const String& fieldName);
    static String decodeXml(String s);
};

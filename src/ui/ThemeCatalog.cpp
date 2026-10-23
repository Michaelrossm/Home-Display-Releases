#include "ThemeCatalog.h"
#include "../services/NetworkWorker.h"
#include "../services/StorageService.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>

namespace hp {
namespace {

constexpr ThemeCatalog::BasePalette kBuiltIns[ThemeCatalog::kBuiltInCount] = {
    // 0 Dunkel - bisheriges Standard-Theme
    {0x07111F,0x0A1625,0x101D2E,0x172A42,0x46627D,0xF5F7FA,0xD2DCE8,0x07111F,0x6EE7B7,0xF4B860,0xF87171,true},
    // 1 AMOLED
    {0x000000,0x050505,0x0B0B0B,0x121212,0x303842,0xF7F7F7,0xB8C0C8,0x05090D,0x62D8A5,0xE9A84F,0xEF6A6A,true},
    // 2 Hell
    {0xF2F5F8,0xE9EEF3,0xFFFFFF,0xF8FAFC,0xCCD6E0,0x18202A,0x667180,0xFFFFFF,0x21895A,0xB36A00,0xC63D4D,false},
    // 3 Nord - kuehles, ruhiges Blau/Grau
    {0x202631,0x252D3A,0x2E3746,0x343E4F,0x536176,0xECEFF4,0xB8C2D1,0x10151D,0x7BC99A,0xE5B567,0xE27676,true},
    // 4 Ozean - tiefes Petrol
    {0x04191E,0x08242A,0x0D3038,0x123B44,0x2B6770,0xF0FBFC,0xAED4D8,0x021014,0x67D6A2,0xF1B65B,0xF47773,true},
    // 5 Sonnenuntergang - warmes dunkles Braun/Violett
    {0x1E1218,0x2A1821,0x38222C,0x432A35,0x765061,0xFFF3EE,0xDCBEB7,0x1A0D12,0x77D6A2,0xF0B45E,0xF27B79,true},
    // 6 Graphit - neutral und kontrastreich
    {0x151719,0x1C1F22,0x25292D,0x2D3237,0x535B63,0xF4F5F6,0xC5C9CD,0x101214,0x70C99A,0xDDB15F,0xE57676,true},
    // 7 Warm Hell - papierartig, weniger kalt als Hell
    {0xF4F0E8,0xECE6DC,0xFFFCF6,0xFAF5EC,0xD4C9B9,0x29251F,0x736B61,0xFFFFFF,0x2F8A61,0xB56C13,0xC4474D,false}
};

constexpr const char* kBuiltInNames[ThemeCatalog::kBuiltInCount] = {
    "Dunkel","AMOLED","Hell","Nord","Ozean","Sonnenuntergang","Graphit","Warm Hell"
};

constexpr const char* kAccentNames[ThemeCatalog::kAccentCount] = {
    "Blau", "Tuerkis", "Gruen", "Orange", "Rot", "Violett"
};

constexpr uint32_t kDarkAccent[6] = {
    0x7CC4FF,0x55D6CF,0x65D69A,0xFFAD5C,0xFF7070,0xB398FF
};
constexpr uint32_t kLightAccent[6] = {
    0x1976D2,0x008A8A,0x278A56,0xC66A00,0xC83B4A,0x7657D6
};

bool parseHex(const char* text, uint32_t& value) {
    if (!text) return false;
    while (*text == ' ' || *text == '\t') ++text;
    if (*text == '#') ++text;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    if (strlen(text) != 6) return false;
    char* end = nullptr;
    const unsigned long parsed = strtoul(text, &end, 16);
    if (!end || *end) return false;
    value = static_cast<uint32_t>(parsed) & 0xFFFFFFu;
    return true;
}

void trim(char* text) {
    if (!text) return;
    char* begin = text;
    while (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n') ++begin;
    if (begin != text) memmove(text, begin, strlen(begin) + 1);
    size_t n = strlen(text);
    while (n && (text[n-1] == ' ' || text[n-1] == '\t' || text[n-1] == '\r' || text[n-1] == '\n'))
        text[--n] = 0;
}

} // namespace

ThemeCatalog& ThemeCatalog::instance() {
    static ThemeCatalog catalog;
    return catalog;
}

const char* ThemeCatalog::accentName(uint8_t accent) {
    return kAccentNames[accent < kAccentCount ? accent : 0];
}

void ThemeCatalog::begin() {
    if (ready_) return;
    ready_ = true;
    // setup() invokes begin before starting LVGL. Runtime calls are cached.
    scanSd();
}

const char* ThemeCatalog::name(uint8_t mode) const {
    if (mode < kBuiltInCount) return kBuiltInNames[mode];
    const uint8_t index = static_cast<uint8_t>(mode - kBuiltInCount);
    return index < sdCount_ ? sd_[index].name : kBuiltInNames[0];
}

const char* ThemeCatalog::filename(uint8_t mode) const {
    if (mode < kBuiltInCount) return "";
    const uint8_t index = static_cast<uint8_t>(mode - kBuiltInCount);
    return index < sdCount_ ? sd_[index].filename : "";
}

uint8_t ThemeCatalog::modeForFilename(const char* filenameValue) const {
    if (!filenameValue || !filenameValue[0]) return 0xFF;
    for (uint8_t i = 0; i < sdCount_; ++i)
        if (!strcmp(sd_[i].filename, filenameValue))
            return static_cast<uint8_t>(kBuiltInCount + i);
    return 0xFF;
}

bool ThemeCatalog::isDark(uint8_t mode) const {
    if (mode < kBuiltInCount) return kBuiltIns[mode].dark;
    const uint8_t index = static_cast<uint8_t>(mode - kBuiltInCount);
    return index < sdCount_ ? sd_[index].palette.dark : true;
}

ThemePalette ThemeCatalog::palette(uint8_t mode, uint8_t accentIndex) const {
    const BasePalette* base = &kBuiltIns[0];
    if (mode < kBuiltInCount) base = &kBuiltIns[mode];
    else {
        const uint8_t index = static_cast<uint8_t>(mode - kBuiltInCount);
        if (index < sdCount_) base = &sd_[index].palette;
    }

    if (accentIndex > 5) accentIndex = 0;
    const uint32_t accent = base->dark ? kDarkAccent[accentIndex] : kLightAccent[accentIndex];

    return {
        base->background, base->backgroundAlt, base->surface, base->card, base->border,
        base->text, base->textSecondary, accent, base->onAccent,
        base->success, base->warning, base->error, base->dark
    };
}

bool ThemeCatalog::loadThemeFile(const String& path, SdTheme& out) {
    auto file = StorageService::instance().openSdRead(path);
    if (!file || file.isDirectory() || file.size() == 0 || file.size() > 1024) {
        if (file) file.close();
        return false;
    }

    BasePalette p = kBuiltIns[0];
    bool haveName = false;
    bool haveBg = false;
    char line[96];

    while (file.available()) {
        size_t n = file.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = 0;
        trim(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = 0;
        trim(line);
        trim(eq);

        uint32_t color = 0;
        if (!strcmp(line, "name")) {
            strlcpy(out.name, eq, sizeof(out.name));
            haveName = out.name[0];
        } else if (!strcmp(line, "dark")) {
            p.dark = atoi(eq) != 0;
        } else if (!strcmp(line, "background") && parseHex(eq, color)) {
            p.background = color; haveBg = true;
        } else if (!strcmp(line, "background_alt") && parseHex(eq, color)) p.backgroundAlt = color;
        else if (!strcmp(line, "surface") && parseHex(eq, color)) p.surface = color;
        else if (!strcmp(line, "card") && parseHex(eq, color)) p.card = color;
        else if (!strcmp(line, "border") && parseHex(eq, color)) p.border = color;
        else if (!strcmp(line, "text") && parseHex(eq, color)) p.text = color;
        else if (!strcmp(line, "text_secondary") && parseHex(eq, color)) p.textSecondary = color;
        else if (!strcmp(line, "on_accent") && parseHex(eq, color)) p.onAccent = color;
        else if (!strcmp(line, "success") && parseHex(eq, color)) p.success = color;
        else if (!strcmp(line, "warning") && parseHex(eq, color)) p.warning = color;
        else if (!strcmp(line, "error") && parseHex(eq, color)) p.error = color;
    }
    file.close();

    if (!haveName || !haveBg) return false;
    out.palette = p;
    return true;
}

void ThemeCatalog::rebuildOptions() {
    optionList_[0] = 0;
    size_t used = 0;

    auto append = [&](const char* name) {
        if (!name || !name[0]) return;
        if (used && used + 1 < sizeof(optionList_)) optionList_[used++] = '\n';
        for (size_t i = 0; name[i] && used + 1 < sizeof(optionList_); ++i)
            optionList_[used++] = name[i];
        optionList_[used] = 0;
    };

    for (uint8_t i = 0; i < kBuiltInCount; ++i) append(kBuiltInNames[i]);
    for (uint8_t i = 0; i < sdCount_; ++i) append(sd_[i].name);
}

void ThemeCatalog::refreshSd() {
    if(resultReady_.load(std::memory_order_acquire)){
        sdCount_=pendingCount_;memcpy(sd_,pendingSd_,sizeof(sd_));rebuildOptions();
        resultReady_.store(false,std::memory_order_release);
        scanPending_.store(false,std::memory_order_release);
    }
    const uint32_t now = millis();
    if (scanned_ && static_cast<uint32_t>(now - lastScanMs_) < 5000UL) return;
    if(scanPending_.exchange(true))return;
    if(!NetworkWorker::instance().submitLocalTransfer(scanWorker,this,"theme-scan")){
        scanPending_=false;return;
    }
    scanned_ = true;
    lastScanMs_ = now;
}

void ThemeCatalog::scanWorker(void* context){
    auto& catalog=*static_cast<ThemeCatalog*>(context);
    ThemeCatalog snapshot;snapshot.scanSd();
    catalog.pendingCount_=snapshot.sdCount_;
    memcpy(catalog.pendingSd_,snapshot.sd_,sizeof(snapshot.sd_));
    catalog.resultReady_.store(true,std::memory_order_release);
}

void ThemeCatalog::scanSd() {
    scanned_=true;lastScanMs_=millis();

    sdCount_ = 0;
    memset(sd_, 0, sizeof(sd_));

    auto& storage = StorageService::instance();
    if (!storage.sdAvailable()) {
        rebuildOptions();
        return;
    }

    File dir = storage.openSdDirectory("/homepanel/themes");
    if (!dir) {
        rebuildOptions();
        return;
    }

    struct Candidate { char name[40]; };
    Candidate candidates[kMaxSdThemes]{};
    uint8_t candidateCount = 0;

    for (File entry = dir.openNextFile(); entry && candidateCount < kMaxSdThemes; entry = dir.openNextFile()) {
        if (entry.isDirectory()) { entry.close(); continue; }
        String filename = entry.name();
        entry.close();
        const int slash = filename.lastIndexOf('/');
        if (slash >= 0) filename = filename.substring(slash + 1);
        if (!filename.endsWith(".theme") || filename.length() >= sizeof(candidates[0].name)) continue;
        strlcpy(candidates[candidateCount++].name, filename.c_str(), sizeof(candidates[0].name));
    }
    dir.close();

    std::sort(candidates, candidates + candidateCount, [](const Candidate& a, const Candidate& b) {
        return strcmp(a.name, b.name) < 0;
    });

    for (uint8_t i = 0; i < candidateCount && sdCount_ < kMaxSdThemes; ++i) {
        SdTheme theme{};
        strlcpy(theme.filename, candidates[i].name, sizeof(theme.filename));
        const String path = String("/homepanel/themes/") + candidates[i].name;
        if (loadThemeFile(path, theme)) sd_[sdCount_++] = theme;
    }

    rebuildOptions();
}

}

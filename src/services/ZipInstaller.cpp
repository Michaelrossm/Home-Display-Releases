#include "ZipInstaller.h"
#include "StorageService.h"
#include <ArduinoJson.h>
extern "C" {
#include <zlib.h>
}
#include <cstring>
#include <vector>
#include <memory>
#include <algorithm>

namespace hp {
namespace {

constexpr uint32_t kEocdSig = 0x06054b50;
constexpr uint32_t kCentralSig = 0x02014b50;
constexpr uint32_t kLocalSig = 0x04034b50;

uint16_t u16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
uint32_t u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct ZipEntry {
    String name;
    uint16_t method = 0;
    uint32_t compSize = 0;
    uint32_t uncompSize = 0;
    uint32_t localOffset = 0;
};

// Central directory scan is enough here; local headers repeat the same sizes.
bool findEocd(const uint8_t* data, size_t len, size_t& eocdOffset) {
    if (len < 22) return false;
    const size_t maxBack = std::min<size_t>(len, 65557);
    for (size_t back = 22; back <= maxBack; ++back) {
        const size_t pos = len - back;
        if (u32(data + pos) == kEocdSig) { eocdOffset = pos; return true; }
    }
    return false;
}

bool listEntries(const uint8_t* data, size_t len, std::vector<ZipEntry>& out) {
    size_t eocd = 0;
    if (!findEocd(data, len, eocd)) return false;
    const uint16_t totalEntries = u16(data + eocd + 10);
    const uint32_t cdOffset = u32(data + eocd + 16);
    if (cdOffset >= len) return false;

    size_t pos = cdOffset;
    for (uint16_t i = 0; i < totalEntries; ++i) {
        if (pos + 46 > len || u32(data + pos) != kCentralSig) return false;
        const uint16_t method = u16(data + pos + 10);
        const uint32_t compSize = u32(data + pos + 20);
        const uint32_t uncompSize = u32(data + pos + 24);
        const uint16_t nameLen = u16(data + pos + 28);
        const uint16_t extraLen = u16(data + pos + 30);
        const uint16_t commentLen = u16(data + pos + 32);
        const uint32_t localOffset = u32(data + pos + 42);
        if (pos + 46 + nameLen > len) return false;

        ZipEntry entry;
        entry.name = String(reinterpret_cast<const char*>(data + pos + 46), nameLen);
        entry.method = method;
        entry.compSize = compSize;
        entry.uncompSize = uncompSize;
        entry.localOffset = localOffset;
        out.push_back(entry);

        pos += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

// Locates the entry's file data by reading its own local file header
// (filename/extra lengths there can differ slightly from the central copy).
bool entryData(const uint8_t* data, size_t len, const ZipEntry& entry, const uint8_t*& out, size_t& outLen) {
    if (entry.localOffset + 30 > len || u32(data + entry.localOffset) != kLocalSig) return false;
    const uint16_t nameLen = u16(data + entry.localOffset + 26);
    const uint16_t extraLen = u16(data + entry.localOffset + 28);
    const size_t dataStart = entry.localOffset + 30 + nameLen + extraLen;
    if (dataStart + entry.compSize > len) return false;
    out = data + dataStart;
    outLen = entry.compSize;
    return true;
}

bool inflateRaw(const uint8_t* src, size_t srcLen, size_t expected, String& out) {
    if (!expected) { out = ""; return true; }
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[expected]);
    if (!buf) return false;

    z_stream strm{};
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) return false;
    strm.next_in = const_cast<uint8_t*>(src);
    strm.avail_in = (uInt)srcLen;
    strm.next_out = buf.get();
    strm.avail_out = (uInt)expected;
    const int rc = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    if (rc != Z_STREAM_END) return false;

    out = String(reinterpret_cast<const char*>(buf.get()), expected);
    return true;
}

bool readEntry(const uint8_t* data, size_t len, const ZipEntry& entry, String& out) {
    const uint8_t* raw = nullptr;
    size_t rawLen = 0;
    if (!entryData(data, len, entry, raw, rawLen)) return false;
    if (entry.method == 0) {
        out = String(reinterpret_cast<const char*>(raw), rawLen);
        return true;
    }
    if (entry.method == 8) return inflateRaw(raw, rawLen, entry.uncompSize, out);
    return false;
}

bool isDirectoryEntry(const ZipEntry& e) { return e.name.endsWith("/") || !e.uncompSize; }

} // namespace

bool ZipInstaller::installFromZip(const uint8_t* data, size_t len, String& outId, String& error) {
    std::vector<ZipEntry> entries;
    if (!listEntries(data, len, entries)) { error = "invalid_zip"; return false; }

    // Find manifest.json and its containing folder prefix (package may be
    // flat, or nested under one or more folders, e.g. Apps/32/strom/).
    int manifestIndex = -1;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].name == "manifest.json" || entries[i].name.endsWith("/manifest.json")) {
            manifestIndex = (int)i;
            break;
        }
    }
    if (manifestIndex < 0) { error = "manifest_missing"; return false; }

    String manifestJson;
    if (!readEntry(data, len, entries[manifestIndex], manifestJson)) { error = "manifest_unreadable"; return false; }
    if (manifestJson.length() > 4096) { error = "manifest_too_large"; return false; }

    JsonDocument doc;
    if (deserializeJson(doc, manifestJson)) { error = "manifest_json"; return false; }
    const char* idField = doc["id"] | "";
    if (!idField[0]) { error = "manifest_id"; return false; }
    for (const char* p = idField; *p; ++p) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') { error = "manifest_id"; return false; }
    }
    const String id = idField;

    const String manifestName = entries[manifestIndex].name;
    const String prefix = manifestName.substring(0, manifestName.length() - String("manifest.json").length());

    auto& st = StorageService::instance();
    if (!st.canStore(StorageClass::AppPackage)) { error = "storage"; return false; }
    auto* fs = st.fsFor(StorageClass::AppPackage);
    if (!fs) { error = "storage_fs"; return false; }

    size_t written = 0;
    for (const auto& entry : entries) {
        if (isDirectoryEntry(entry)) continue;
        if (!entry.name.startsWith(prefix)) continue;
        String relative = entry.name.substring(prefix.length());
        if (!relative.length()) continue;

        String content;
        if (!readEntry(data, len, entry, content)) { error = "extract_failed:" + entry.name; return false; }

        const String targetRel = "/" + id + "/" + relative;
        if (!st.ensureParentDirs(StorageClass::AppPackage, targetRel)) {
            error = "mkdir_failed:" + relative;
            return false;
        }
        // Direct write: a fresh install has no existing file to protect, so
        // skip the temp-file/rename dance (one less failure-prone step).
        const String targetPath = st.pathFor(StorageClass::AppPackage, targetRel);
        File f = fs->open(targetPath, FILE_WRITE);
        if (!f) { error = "open_failed:" + relative; return false; }
        const size_t got = content.length() ? f.write((const uint8_t*)content.c_str(), content.length()) : 0;
        f.flush();
        f.close();
        if (got != content.length()) { error = "write_short:" + relative; return false; }
        ++written;
    }

    if (!written) { error = "empty_package"; return false; }
    outId = id;
    return true;
}

}

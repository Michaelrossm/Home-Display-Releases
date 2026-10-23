#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
class String : public std::string {
public:
    using std::string::string;
    String(const std::string& s) : std::string(s) {}
    int indexOf(char c) const { auto i=find(c); return i==npos ? -1 : static_cast<int>(i); }
    String substring(size_t begin, size_t end) const { return substr(begin, end-begin); }
};
namespace fs {
struct FS {
    std::map<String, String> files;
    String failSource;
    bool exists(const String& path) { return files.count(path) != 0; }
    bool remove(const String& path) { return files.erase(path) != 0; }
    bool rename(const String& from, const String& to) {
        if (from == failSource || !exists(from) || exists(to)) return false;
        files[to] = files[from];
        files.erase(from);
        return true;
    }
};
}
// PRODUCTION_COMMIT_FUNCTION
struct esp_partition_t { size_t size; } partition{1025};
std::vector<uint8_t> flash(partition.size, 0xff);
int failedReadOffset = -1;
constexpr int ESP_PARTITION_TYPE_DATA=1, ESP_PARTITION_SUBTYPE_DATA_SPIFFS=2, ESP_OK=0;
const esp_partition_t* esp_partition_find_first(int, int, const char*) { return &partition; }
int esp_partition_read(const esp_partition_t*, size_t offset, void* out, size_t count) {
    if (static_cast<int>(offset) == failedReadOffset) return -1;
    assert(offset+count <= flash.size());
    memcpy(out, flash.data()+offset, count);
    return ESP_OK;
}
void taskYIELD() {}
struct StorageService {
    bool validateRelativePath(const String&) const;
    bool partitionLooksBlank(const char*) const;
};
// PRODUCTION_STORAGE_FUNCTIONS
int main() {
    StorageService storage;
    for (const char* path : {"..", ".", "../a", "a/../b", "/a/..", "a\\b", "a/./b", "a\nb"})
        assert(!storage.validateRelativePath(path));
    for (const char* path : {"/", "main.lua", "/Apps/32/demo/main.lua", "two dots..txt"})
        assert(storage.validateRelativePath(path));
    assert(!storage.validateRelativePath(String(181, 'a')));
    assert(storage.partitionLooksBlank("appfs"));
    flash.back()=0;
    assert(!storage.partitionLooksBlank("appfs")); // erased header, damaged tail
    flash.back()=0xff;
    failedReadOffset=512;
    assert(!storage.partitionLooksBlank("appfs")); // never format on read failure

    fs::FS disk;
    disk.files = {{"file", "old"}, {"file.part", "new"}};
    assert(commitUploadedFile(&disk, "file.part", "file"));
    assert(disk.files["file"] == "new" && !disk.exists("file.bak"));

    disk.files = {{"file", "old"}, {"file.part", "new"}};
    disk.failSource = "file.part";
    assert(!commitUploadedFile(&disk, "file.part", "file"));
    assert(disk.files["file"] == "old");

    disk.files = {{"file", "old"}, {"file.part", "new"}};
    disk.failSource = "file";
    assert(!commitUploadedFile(&disk, "file.part", "file"));
    assert(disk.files["file"] == "old" && disk.files["file.part"] == "new");

    disk.files = {{"file.bak", "old"}, {"file.part", "new"}};
    disk.failSource = "file.part";
    assert(!commitUploadedFile(&disk, "file.part", "file"));
    assert(disk.files["file"] == "old");

    disk.files = {{"file.bak", "old"}, {"file.part", "new"}};
    disk.failSource = "file.bak";
    assert(!commitUploadedFile(&disk, "file.part", "file"));
    assert(disk.files["file.bak"] == "old");

    disk.files = {{"file.part", "new"}};
    disk.failSource.clear();
    assert(commitUploadedFile(&disk, "file.part", "file"));
    assert(disk.files["file"] == "new");

    disk.files = {{"file", "committed"}, {"file.bak", "older"}, {"file.part", "new"}};
    disk.failSource = "file.part";
    assert(!commitUploadedFile(&disk, "file.part", "file"));
    assert(disk.files["file"] == "committed");
}

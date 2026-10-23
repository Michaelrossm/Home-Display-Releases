#pragma once
#include "FileBrowserService.h"

namespace hp {
// Exposes only the mounted LittleFS SystemData and Config namespaces.
// No raw firmware, NVS, credentials store, or flash partitions are accessed.
class InternalFilesService {
public:
    static bool isUri(const String& uri);
    static std::shared_ptr<DirectoryRequest> list(const String& uri,size_t offset=0);
    static bool readRange(const String& uri,uint8_t* buffer,size_t cap,size_t& read,uint64_t& total,const std::atomic<bool>* cancel=nullptr);
private:
    static void run(void* context);
};
}

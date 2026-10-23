#pragma once
#include <Arduino.h>
#include <cstdio>
#include <atomic>
#include <memory>
namespace hp {
// Decoder task only consumes FILE bytes. All source I/O runs on NetworkWorker.
class AudioFileStream {
public:
    static FILE* open(const String& uri,std::shared_ptr<std::atomic<bool>> canceled);
};
}

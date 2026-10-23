#pragma once
#include <Arduino.h>
#include <atomic>
namespace hp {
struct MemoryDiagnostics{uint32_t freeInternal=0,minInternal=0,largestInternal=0,freePsram=0,minPsram=0;};
class DiagnosticsService{
public:
    static DiagnosticsService&instance();
    MemoryDiagnostics memory()const;
    String supportReportText()const;
    String redactUrl(const String&)const;
    void recordHttpFailure(){httpFailures_.fetch_add(1,std::memory_order_relaxed);}
    uint32_t httpFailures()const{return httpFailures_.load(std::memory_order_relaxed);}
private:
    std::atomic<uint32_t> httpFailures_{0};
};
}

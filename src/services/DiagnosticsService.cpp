#include "DiagnosticsService.h"
#include "UiPerformanceMonitor.h"
#include "../apps/lua/LuaJobService.h"
#include "BuildInfo.h"
#include "ConfigService.h"
#include "NetworkService.h"
#include "StorageService.h"
#include "OtaService.h"
#include "OtaTrace.h"
#include "NetworkWorker.h"
#include "WebServerService.h"
#include "FileTransferService.h"
#include "WebCsrfService.h"
#include "../core/BootManager.h"
#include "../core/SystemSupervisor.h"
#include "../core/EventLog.h"
#include "../core/PerformancePolicy.h"
#include "../infrastructure/TransactionJournal.h"
#include "../infrastructure/AssetManager.h"
#include "../core/BackgroundScheduler.h"
#include "AudioHardwareService.h"
#include "../ui/UiShell.h"
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>

namespace hp {
DiagnosticsService& DiagnosticsService::instance(){static DiagnosticsService d;return d;}
MemoryDiagnostics DiagnosticsService::memory()const{
    MemoryDiagnostics m;
    m.freeInternal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    m.minInternal=heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    m.largestInternal=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    m.freePsram=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    m.minPsram=heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);return m;
}
String DiagnosticsService::redactUrl(const String&in)const{
    String u=in;int scheme=u.indexOf("://");if(scheme>=0){int at=u.indexOf('@',scheme+3);int slash=u.indexOf('/',scheme+3);if(at>=0&&(slash<0||at<slash))u=u.substring(0,scheme+3)+"***:***@"+u.substring(at+1);}
    int fragment=u.indexOf('#');if(fragment>=0)u=u.substring(0,fragment);int q=u.indexOf('?');if(q>=0)u=u.substring(0,q)+"?[redacted]";return u;
}
String DiagnosticsService::supportReportText()const{
    auto m=memory();const esp_partition_t*r=esp_ota_get_running_partition();String s;s.reserve(3500);
    s+="Home-Display Support Report\n";
    s+="version="+String(build::kVersion)+" profile="+build::kProfile+"\n";
    s+="uptime_s="+String(millis()/1000)+" reset="+String(BootManager::instance().state().resetReason)+" previous_reset="+String(BootManager::instance().state().previousResetReason)+"\n";
    s+="boot_slot="+String(r?r->label:"unknown")+" boot_count="+String(BootManager::instance().state().bootCount)+" failed_boots="+String(BootManager::instance().state().failedBootCount)+"\n";
    s+="ota_state="+String((int)OtaService::instance().state())+" journal_state="+String((int)TransactionJournal::instance().state())+"\n";
    const auto otaTrace=OtaTrace::snapshot();
    s+="ota_last_phase="+String(otaTrace.phase)+" ota_last_bytes="+String(otaTrace.bytes)+" ota_last_error="+String(otaTrace.error)+"\n";
    s+="config_schema="+String(ConfigService::instance().schema())+" systemdata="+String((int)AssetManager::instance().systemDataStatus())+" appfs="+String((int)AssetManager::instance().appFsStatus())+" sd="+String(StorageService::instance().sdAvailable())+"\n";
    s+="heap_free="+String(m.freeInternal)+" heap_min="+String(m.minInternal)+" heap_largest="+String(m.largestInternal)+" psram_free="+String(m.freePsram)+" psram_min="+String(m.minPsram)+"\n";
    s+="network="+String(NetworkService::instance().pathName())+" wifi_reconnects="+String(NetworkService::instance().wifiReconnects())+" eth_reconnects="+String(NetworkService::instance().ethernetReconnects())+"\n";
    s+="net_queue_pending="+String(NetworkWorker::instance().pending())+
        " net_priority_pending="+String(NetworkWorker::instance().priorityDepth())+
        " net_max_wait_ms="+String(NetworkWorker::instance().maxWaitMs())+
        " net_max_job_ms="+String(NetworkWorker::instance().maxJobMs())+
        " net_long_jobs="+String(NetworkWorker::instance().longJobs())+"\n";
    for(bool transfer:{false,true}){
        const auto wait=NetworkWorker::instance().waitLatency(transfer);
        const auto run=NetworkWorker::instance().jobLatency(transfer);
        s+=String(transfer?"net_transfer":"net_normal")+" samples="+String(run.samples)+
            " wait_p50_ms="+String(wait.p50Ms)+" wait_p95_ms="+String(wait.p95Ms)+" wait_p99_ms="+String(wait.p99Ms)+
            " run_p50_ms="+String(run.p50Ms)+" run_p95_ms="+String(run.p95Ms)+" run_p99_ms="+String(run.p99Ms)+"\n";
    }
    s+="net_queue_peak="+String(NetworkWorker::instance().queuePeak())+"\n";
    const auto jobs=LuaJobService::instance().diagnostics();
    s+="lua_jobs_completed="+String(jobs.completed)+" lua_jobs_cancelled="+String(jobs.cancelled)+
        " lua_jobs_timeouts="+String(jobs.timeouts)+" lua_jobs_retry_eligible="+String(jobs.retryable)+" lua_jobs_coalesced="+String(jobs.coalesced)+" lua_jobs_auto_retries=0\n";
    s+="performance_ui_core="+String(performance::kUiCore)+" performance_io_core="+String(performance::kIoCore)+" c6_radio_burst="+String(NetworkWorker::instance().radioBurstActive()?1:0)+" c6_compute_offload=0\n";
    s+="storage_read_errors="+String(StorageService::instance().readErrors())+" storage_write_errors="+String(StorageService::instance().writeErrors())+" sd_max_read_us="+String(StorageService::instance().maxReadUs())+" sd_max_write_us="+String(StorageService::instance().maxWriteUs())+"\n";
    s+="ui_fps="+String(UiShell::instance().fps())+" ui_max_frame_us="+String(UiShell::instance().maxFrameUs())+" bg_stack_hwm="+String(BackgroundScheduler::instance().stackHighWater())+" bg_worker_hwm="+String(BackgroundScheduler::instance().workerStackHighWater())+" bg_quick_max_us="+String(BackgroundScheduler::instance().maxQuickUs())+" bg_block_max_us="+String(BackgroundScheduler::instance().maxBlockingUs())+" bg_queue_rejects="+String(BackgroundScheduler::instance().queueRejects())+" net_stack_hwm="+String(NetworkWorker::instance().stackHighWater())+"\n";
    s+="http_failures="+String(httpFailures())+" audio_underruns="+String(AudioHardwareService::instance().underruns())+" supervisor_events="+String(SystemSupervisor::instance().watchdogEvents())+" net_queue_rejects="+String(NetworkWorker::instance().failedSubmit())+"\n";
    s+="event_persist_drops="+String(EventLog::instance().persistenceDrops())+" event_persist_failures="+String(EventLog::instance().persistenceFailures())+"\n";
    using UiPhase=UiShell::Phase;
    s+="ui_resources_max_us="+String(UiShell::instance().maxPhaseUs(UiPhase::Resources))+" ui_navigation_max_us="+String(UiShell::instance().maxPhaseUs(UiPhase::Navigation))+" ui_callbacks_max_us="+String(UiShell::instance().maxPhaseUs(UiPhase::Callbacks))+" ui_housekeeping_max_us="+String(UiShell::instance().maxPhaseUs(UiPhase::Housekeeping))+"\n";
    const auto& uiPerf=UiPerformanceMonitor::instance();
    s+="ui_loop_hz="+String(UiShell::instance().fps())+" ui_widget_changes="+String(uiPerf.widgetChanges())+" ui_widget_skips="+String(uiPerf.widgetSkips())+
       " lvgl_flush_count="+String(uiPerf.flushCount())+" lvgl_flush_pixels="+String((unsigned long long)uiPerf.flushPixels())+
       " lvgl_flush_max_pixels="+String(uiPerf.maxFlushPixels())+" lvgl_refresh_period_ms="+String(uiPerf.refreshPeriod())+"\n";
    s+="lvgl_perf_attached="+String(uiPerf.attached()?1:0)+
       " lvgl_render_fps="+String(uiPerf.renderFps())+
       " lvgl_rendered_refreshes="+String(uiPerf.completedRefreshes())+
       " lvgl_empty_refreshes="+String(uiPerf.noDrawRefreshes())+
       " ui_navigation_pending="+String(uiPerf.navigationPending())+"\n";
    auto metric=[&s](const char* name,const UiPerformanceMonitor::Metric& v){
        s+=String(name)+"_samples="+String(v.samples)+
           " "+name+"_p50_ms="+String(v.p50Ms)+
           " "+name+"_p95_ms="+String(v.p95Ms)+
           " "+name+"_p99_ms="+String(v.p99Ms)+
           " "+name+"_max_us="+String(v.maxUs)+" "+name+"_mean_us="+String(v.meanUs)+"\n";
    };
    metric("lvgl_render",uiPerf.render());
    metric("ui_loop_wall",uiPerf.uiLoops());
    metric("lvgl_flush_callback",uiPerf.flushCallback());
    metric("ui_app_build",uiPerf.appBuilds());
    metric("ui_cold_start",uiPerf.coldStarts());
    metric("ui_warm_start",uiPerf.warmStarts());
    metric("ui_visible_navigation",uiPerf.visibleNavigation());
    metric("ui_first_meaningful",uiPerf.firstMeaningfulNavigation());
    metric("lua_script_read",uiPerf.luaReads());
    metric("lua_script_compile",uiPerf.luaCompiles());
    metric("ui_home_build",uiPerf.homeBuilds());
    s+="web80_active="+String(WebServerService::instance().running()?1:0)+" web80_port=443 web80_tls=1 web80_ctrl=32769 web80_max_sockets="+String((unsigned long)WebServerService::socketCapacity())+" web80_clients="+String((unsigned long)WebServerService::instance().activeClients())+" web80_free_sockets="+String((unsigned long)WebServerService::instance().freeSockets())+" ";
    s+="web8088_active="+String(FileTransferService::instance().running()?1:0)+" web8088_port=8088 web8088_tls=1 web8088_ctrl=32768 web8088_max_sockets=1 web8088_clients="+String((unsigned long)FileTransferService::instance().activeClients())+" web8088_free_sockets="+String((unsigned long)FileTransferService::instance().freeSockets())+" ";
    s+="csrf_failures="+String(WebCsrfService::instance().failures())+"\n";
    s+="event_warnings="+String(EventLog::instance().warningCount())+" event_errors="+String(EventLog::instance().errorCount())+"\n";
    const String recent=EventLog::instance().recentText(5,true);if(recent.length())s+="recent_events:\n"+recent+"\n";
    if(BootManager::instance().previousFailure().length())s+="previous_failure="+BootManager::instance().previousFailure()+"\n";
    return s;
}
}

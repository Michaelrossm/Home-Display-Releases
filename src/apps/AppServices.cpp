#include "AppServices.h"
#include "../core/EventLog.h"
#include "radio/RadioService.h"
#include "music/MusicPlayerService.h"
#include "weather/WeatherService.h"
#include "power/PowerService.h"
#include "calendar/CalendarService.h"
#include "goliath/GoliathService.h"
#include "../services/EnergyHistoryService.h"
#include "../services/PrinterService.h"
#include "../services/PhotoLibraryService.h"
#include "../services/IntercomService.h"
#include "../services/IntercomAudioService.h"
#include "../services/VideoService.h"
#include "../core/AppBackgroundScheduler.h"

namespace hp {
bool beginSharedAppServices() {
    bool ok = true;
    ok &= RadioService::instance().begin();
    ok &= MusicPlayerService::instance().begin();
    ok &= WeatherService::instance().begin();
    ok &= PowerService::instance().begin();
    ok &= EnergyHistoryService::instance().begin();
    ok &= PrinterService::instance().begin();
    ok &= PhotoLibraryService::instance().begin();
    ok &= AppBackgroundScheduler::instance().begin();
    ok &= CalendarService::instance().begin();
#if HOMEPANEL_FEATURE_INTERCOM
    ok &= GoliathService::instance().begin();
    ok &= IntercomService::instance().begin();
    ok &= IntercomAudioService::instance().begin();
#endif
    ok &= VideoService::instance().begin();
    if (!ok) {
        EventLog::instance().add("WARN", "APP_BACKENDS", "One or more shared Lua app backends failed");
    }
    return ok;
}
}

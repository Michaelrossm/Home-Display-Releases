#pragma once
#include "CalendarSyncService.h"
namespace hp {
// Public provider-neutral name for the EXISTING runtime; no second singleton,
// credential registry, network task or OAuth state machine.
using CloudAccountService=CalendarSyncService;
}

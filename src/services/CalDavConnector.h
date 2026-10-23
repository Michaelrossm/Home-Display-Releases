#pragma once
#include "CloudAccountTypes.h"
#include "CalDavDiscovery.h"
#include "PhonebookService.h"
#include "../core/RequestDeadline.h"
#include "../apps/calendar/CalendarImport.h"
namespace hp {
// Worker-local operation budget; no task/timer, no secret or Lua-state pointer.
struct CalDavControl{
 RequestDeadline deadline{millis(),20000};
 bool(*current)(void*)=nullptr;void* context=nullptr;
 bool stopped()const{return !deadline.remaining(millis())||(current&&!current(context));}
 uint32_t nextTimeout()const{return stopped()?0:deadline.stepTimeout(millis(),8000);}
};
struct CalDavConnectionResult {CloudAccountState state=CloudAccountState::Error;String message;std::vector<CloudCalendarCollection> calendars;bool truncated=false;};
struct CardDavConnectionResult {CloudAccountState state=CloudAccountState::Error;String message;std::vector<PhonebookContact> contacts;bool truncated=false;};
struct CalDavEventsResult {CloudAccountState state=CloudAccountState::Error;String message;std::vector<calendar::ImportedEvent> events;bool truncated=false;};
class CalDavConnector {
public:
 // Blocking transport: called exclusively by the existing NetworkWorker.
 static CalDavConnectionResult discover(const char* url,const char* user,const char* password,bool icloud,CalDavControl* control=nullptr);
 static CardDavConnectionResult contacts(const char* url,const char* user,const char* password,bool icloud,CalDavControl* control=nullptr);
 static CalDavEventsResult events(const char* root,const char* collection,const char* user,const char* password,bool icloud,int64_t from,int64_t to,CalDavControl* control=nullptr);
};
}

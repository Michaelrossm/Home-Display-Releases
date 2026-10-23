#pragma once
#include "EmailAccountService.h"
#include <cstring>
namespace hp {
enum class AccountAuthState : uint8_t {Idle,RequestingCode,AwaitingUser,Polling,Connected,Expired,Cancelled,Error};
enum class OAuthPurpose : uint8_t {Identity,Calendar,Contacts,CalendarContacts};
namespace AccountAuthPolicy {
inline bool hasCalendar(OAuthPurpose p){return p==OAuthPurpose::Calendar||p==OAuthPurpose::CalendarContacts;}
inline bool hasContacts(OAuthPurpose p){return p==OAuthPurpose::Contacts||p==OAuthPurpose::CalendarContacts;}
// Preserve encrypted v1/v2 record size and old purpose tags (0=calendar,1=profile).
inline uint8_t purposeTag(OAuthPurpose p){return p==OAuthPurpose::Identity?1:p==OAuthPurpose::Contacts?2:p==OAuthPurpose::CalendarContacts?3:0;}
inline OAuthPurpose fromTag(uint8_t tag){return tag==1?OAuthPurpose::Identity:tag==2?OAuthPurpose::Contacts:tag==3?OAuthPurpose::CalendarContacts:OAuthPurpose::Calendar;}
inline bool scopeHas(const String& scopes,const char* expected,const char* alias=nullptr){
 const char* p=scopes.c_str();while(*p){while(*p==' ')++p;const char* start=p;while(*p&&*p!=' ')++p;const size_t n=p-start;
 if((n==strlen(expected)&&!strncmp(start,expected,n))||(alias&&n==strlen(alias)&&!strncmp(start,alias,n)))return true;}return false;
}
inline const char* stateName(AccountAuthState state){
    const char* names[]={"Idle","RequestingCode","AwaitingUser","Polling","Connected","Expired","Cancelled","Error"};
    return uint8_t(state)<8?names[uint8_t(state)]:"Error";
}
inline bool active(AccountAuthState state){return state==AccountAuthState::RequestingCode||state==AccountAuthState::AwaitingUser||state==AccountAuthState::Polling;}
inline bool deviceFlow(EmailProvider provider,OAuthPurpose purpose){return provider==EmailProvider::Microsoft||purpose==OAuthPurpose::Identity;}
inline bool validClientId(const String& id){
    if(!id.length()||id.length()>192)return false;
    for(size_t i=0;i<id.length();++i){const uint8_t c=id[i];if(!isalnum(c)&&c!='-'&&c!='_'&&c!='.')return false;}return true;
}
inline String resolveClientId(const String& standard,const String& custom,bool own){return own?custom:standard;}
inline bool current(uint32_t generation,uint32_t expected,uint32_t expires,uint32_t now){
    return generation==expected&&(!expires||int32_t(now-expires)<0);
}
inline uint32_t remaining(uint32_t expires,uint32_t now){return expires&&int32_t(expires-now)>0?(uint32_t(expires-now)+999)/1000:0;}
}
}

#pragma once
#include <Arduino.h>
#include <time.h>
namespace hp::OAuthProtocol {
String encode(const String&);
bool httpsRedirect(const String&);
bool constantEqual(const String&,const String&);
bool callbackAllowed(const String& expected,const String& received,uint32_t expires,uint32_t now,bool consumed,bool pending);
uint32_t pollDelay(uint32_t interval,bool slowDown);
String randomToken();
String challenge(const String&);
int64_t utcDate(const char*);
String utcText(int64_t);
}

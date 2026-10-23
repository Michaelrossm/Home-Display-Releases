from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]
class CalendarJsonOwnershipTests(unittest.TestCase):
    def test_serialization_does_not_reference_record_buffers(self):
        source=(ROOT/'src/apps/calendar/CalendarApi.cpp').read_text(encoding='utf-8')
        methods='void eventJson('+source.split('void eventJson(',1)[1].split('bool detailsRead(',1)[0]
        fixture=test_system_performance.SystemPerformanceTests().string_fixture()+r'''
#include <ArduinoJson.h>
namespace ArduinoJson {template<>struct Converter<String>{static void toJson(const String& s,JsonVariant v){v.set(std::string(s));}};}
struct EventRecord{uint64_t localId=1;uint32_t calendarId=1,revision=1;int64_t startUtc=1,endUtc=2;char title[72]{},location[64]{};uint16_t participantMask=0,flags=0;};
struct EventDetails{char location[64]{},street[64]{},houseNumber[32]{},postalCode[32]{},city[64]{},region[32]{},country[32]{},phone[64]{},notes[256]{};bool notesHtml=false,truncated=false;};
constexpr unsigned EventAllDay=1,EventReadOnly=2,EventRecurring=4;
String identifier(uint64_t){return "id";}String identity32(uint32_t){return "id";}
'''+methods+r'''
int main(){JsonDocument doc;EventRecord record;strcpy(record.title,"Termin privat");strcpy(record.location,"München");eventJson(doc.to<JsonObject>(),record);
 memset(record.title,'X',sizeof(record.title)-1);memset(record.location,'Y',sizeof(record.location)-1);
 assert(std::string(doc["title"].as<const char*>())=="Termin privat");assert(std::string(doc["location"].as<const char*>())=="München");
 EventDetails detail;strcpy(detail.notes,"Notizen bleiben erhalten");strcpy(detail.street,"Teststraße");detailsJson(doc.to<JsonObject>(),detail);memset(detail.notes,'Z',sizeof(detail.notes)-1);memset(detail.street,'Z',sizeof(detail.street)-1);
 std::string json;serializeJson(doc,json);assert(json.find("Notizen bleiben erhalten")!=std::string::npos);assert(json.find("Teststraße")!=std::string::npos);
}
'''
        test_system_performance.SystemPerformanceTests.native(self,fixture,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])
        self.assertIn('row["name"]=String(c.name)',source)
        self.assertIn('row["name"]=String(p.displayName)',source)

if __name__=='__main__':unittest.main()

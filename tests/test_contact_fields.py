from pathlib import Path
import unittest
import test_system_performance
from test_native_phonebook import function

ROOT=Path(__file__).resolve().parents[1]

class ContactFieldsTests(unittest.TestCase):
    def native(self,code,includes=()):
        test_system_performance.SystemPerformanceTests().native(code,includes)

    def test_actual_vcard_roundtrip_and_labels(self):
        self.native(f'#include "{(ROOT/"src/services/ContactProtocol.cpp").as_posix()}"\n'+r'''
#include <cassert>
int main(){using namespace hp::ContactProtocol;Card c;
assert(parseVcard("BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Anna Müller\r\nBDAY:1990-02-28\r\nORG:Firma\r\nTITLE:Leitung\r\nitem1.TEL;TYPE=HOME:+49123\r\nitem2.TEL;TYPE=WORK:+49456\r\nitem2.X-ABLabel:Zentrale\r\nEMAIL;TYPE=HOME:a@example.com\r\nEMAIL;TYPE=WORK:b@example.com\r\nADR;TYPE=WORK:;;Hauptstraße 12;Köln;NRW;50667;Deutschland\r\nNOTE:Zeile 1\\nZeile 2\r\nEND:VCARD\r\n",c));
assert(c.phones.size()==2&&c.emails.size()==2&&c.addresses.size()==1);
assert(c.phones[0].label=="Privat"&&c.phones[1].label=="Zentrale");assert(c.emails[1].label=="Arbeit");
assert(c.birthday.year==1990&&c.company=="Firma"&&c.jobTitle=="Leitung");
c.notes=std::string(100,'x')+"ü"+std::string(100,'y');std::string encoded;assert(serializeVcard(c,encoded));
size_t at=0;while(at<encoded.size()){auto end=encoded.find("\r\n",at);assert(end!=std::string::npos&&end-at<=75);at=end+2;}
Card d;assert(parseVcard(encoded,d));assert(d.name==c.name&&d.notes==c.notes&&d.phones[1].label=="Zentrale"&&d.addresses[0].region=="NRW");
assert(d.birthday.year==1990&&d.emails[1].value=="b@example.com");
c.emails[0].label="Mobil";assert(serializeVcard(c,encoded));assert(encoded.find("EMAIL;TYPE=cell")==std::string::npos);assert(parseVcard(encoded,d)&&d.emails[0].label=="Mobil");
c.birthday={0,2,29};assert(serializeVcard(c,encoded));assert(parseVcard(encoded,d)&&d.birthday.year==0&&d.birthday.day==29);
for(unsigned i=0;i<7;++i)c.phones.push_back({"Privat","123"});assert(!serializeVcard(c,encoded)&&encoded.empty());
std::string tooMany="BEGIN:VCARD\nVERSION:4.0\nFN:Test\n";for(unsigned i=0;i<9;++i)tooMany+="TEL:123\n";tooMany+="END:VCARD\n";assert(!parseVcard(tooMany,d));
assert(!parseVcard("BEGIN:VCARD\nVERSION:4.0\nFN:Test\nBDAY:2023-02-29\nEND:VCARD",d));
Card invalid;invalid.name="Name";invalid.phones={{"Privat",std::string(96,char(0x80))}};assert(!serializeVcard(invalid,encoded));
}
''')

    def test_actual_birthday_calendar_occurrences(self):
        self.native(f'#include "{(ROOT/"src/services/ContactProtocol.cpp").as_posix()}"\n'+
                    f'#include "{(ROOT/"src/apps/calendar/ContactBirthday.h").as_posix()}"\n'+r'''
#include <cassert>
struct Contact{std::string id="stable-contact",name="Anna";hp::ContactProtocol::Birthday birthday;};
struct Event{uint64_t localId=0;uint32_t calendarId=0;int64_t startUtc=0,endUtc=0;char title[72]{};};
int main(){using namespace hp;ContactProtocol::Birthday b;
assert(ContactProtocol::parseBirthday("--02-29",b)&&b.year==0);assert(ContactProtocol::parseBirthday("19900228",b)&&b.year==1990);
assert(!ContactProtocol::parseBirthday("1990-02-29",b));assert(!ContactProtocol::parseBirthday("2024-13-01",b));
assert(!ContactProtocol::parseBirthday("1899-01-01",b));assert(ContactProtocol::parseBirthday("",b)&&!b.month);
Contact c;Event e;c.birthday={2000,2,29};assert(calendar::contactBirthday(c,2025,7,e));
auto date=calendar::localDate(e.startUtc);assert(date.year==2025&&date.month==2&&date.day==28);assert(e.calendarId==7&&std::string(e.title).find("25.")!=std::string::npos);
const auto id=e.localId;Event second;assert(calendar::contactBirthday(c,2025,7,second)&&second.localId==id);
assert(calendar::contactBirthday(c,2024,7,e));date=calendar::localDate(e.startUtc);assert(date.day==29&&e.localId!=id);
assert(!calendar::contactBirthday(c,1999,7,e));c.birthday={0,3,5};assert(calendar::contactBirthday(c,2026,7,e)&&std::string(e.title)=="Geburtstag: Anna");
c.birthday={};assert(!calendar::contactBirthday(c,2026,7,e));
}
''')

    def test_actual_editor_preserves_empty_row_indices(self):
        source=(ROOT/'src/apps/phonebook/PhonebookApp.cpp').read_text(encoding='utf-8')
        draft=function(source,'bool draft(')
        self.native(f'#include "{(ROOT/"src/services/ContactProtocol.cpp").as_posix()}"\n'+r'''
#include <cassert>
namespace ContactProtocol=hp::ContactProtocol;
struct String:std::string{using std::string::string;using std::string::operator=;};
String personDisplayName(const String& first,const String& last){String result=first;if(first.size()&&last.size())result+=" ";result+=last;return result;}
struct Contact{String name,firstName,lastName,company,jobTitle,notes,userId,email,phone,mobile,street,houseNumber,postalCode,city,country;bool readOnly=false;
ContactProtocol::Birthday birthday;std::vector<ContactProtocol::Value> phones,emails;std::vector<ContactProtocol::Address> addresses;};
struct lv_obj_t{std::string text;unsigned selected=0;};
const char* lv_textarea_get_text(lv_obj_t* o){return o->text.c_str();}unsigned lv_dropdown_get_selected(lv_obj_t* o){return o->selected;}
void lv_label_set_text(lv_obj_t* o,const char* s){o->text=s;}
struct Page{Contact contact;struct User{String id;};struct Data{std::vector<User> users;}data;
lv_obj_t* fields[6];lv_obj_t *assignment,*status;
struct ValueInputs{lv_obj_t *label,*value;};std::vector<ValueInputs> phoneInputs,emailInputs;
struct AddressInputs{lv_obj_t* fields[8];};std::vector<AddressInputs> addressInputs;
'''+draft+r'''
};
int main(){lv_obj_t name{"Anna"},company{"Firma"},title{"Leitung"},notes{"Notiz"},birthday{"--02-29"},owner{},status{},label{"Privat"},empty{},value{"123"};
Page p;p.fields[0]=&name;p.fields[1]=&company;p.fields[2]=&title;p.fields[3]=&notes;p.fields[4]=&birthday;p.fields[5]=&empty;p.assignment=&owner;p.status=&status;
p.phoneInputs={{&label,&empty},{&label,&value}};assert(p.draft());assert(p.contact.phones.size()==2&&p.contact.phones[1].value=="123");
assert(p.draft(true)&&p.contact.phones.size()==1&&p.contact.phone=="123");birthday.text="2023-02-29";assert(!p.draft());
}
''')

    def test_integration_bounds_and_no_duplicate_event_store(self):
        service=(ROOT/'src/services/PhonebookService.cpp').read_text(encoding='utf-8')
        query=(ROOT/'src/apps/calendar/CalendarOperations.cpp').read_text(encoding='utf-8')
        app=(ROOT/'src/apps/phonebook/PhonebookApp.cpp').read_text(encoding='utf-8')
        self.assertIn('row["phones"].isNull()',service)
        self.assertIn('row["addresses"].isNull()',service)
        self.assertIn('c.phones.size()>8',service)
        self.assertIn('c.addresses.size()>4',service)
        self.assertIn('++revision_',service)
        self.assertIn('loadLocal(contacts)',query)
        self.assertIn('EventAllDay|EventReadOnly;add',query)
        self.assertNotIn('saveEvent',function(service,'bool PhonebookService::changeLocal('))
        self.assertIn('p.draft(true)',app)
        self.assertIn('importVcardFile',app)
        self.assertIn('exportVcardFile',app)
        self.assertNotIn('xTaskCreate',app)

    def test_actual_exchange_filename_is_not_arbitrary_storage_path(self):
        source=(ROOT/'src/services/PhonebookService.cpp').read_text(encoding='utf-8')
        self.native(r'''
#include <string>
#include <algorithm>
#include <cctype>
#include <cassert>
struct String:std::string {using std::string::string;String(const std::string& s):std::string(s){};
String substring(size_t n)const{return substr(n);}void toLowerCase(){for(char& c:*this)c=tolower((unsigned char)c);}int indexOf(const char* s)const{auto n=find(s);return n==npos?-1:int(n);}};
'''+function(source,'bool exchangeFilename(')+r'''
int main(){assert(exchangeFilename("kontakt.vcf"));assert(exchangeFilename("Firma-1.VCF"));
assert(!exchangeFilename("../accounts/secret.vcf"));assert(!exchangeFilename("a/b.vcf"));assert(!exchangeFilename(".vcf"));
assert(!exchangeFilename("x..vcf"));assert(!exchangeFilename("x.txt"));assert(!exchangeFilename("a:b.vcf"));assert(!exchangeFilename("a\\b.vcf"));
assert(!exchangeFilename(std::string(81,'x')+".vcf"));}
''')

    def test_actual_json_migration_and_owned_strings(self):
        json_header=ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src/ArduinoJson.h'
        if not json_header.exists():self.skipTest('Pinned ArduinoJson is installed by the firmware build')
        source=(ROOT/'src/services/PhonebookService.cpp').read_text(encoding='utf-8')
        self.native(f'#include "{json_header.as_posix()}"\n'+
                    f'#include "{(ROOT/"src/services/ContactProtocol.cpp").as_posix()}"\n'+r'''
#include <cassert>
using String=std::string;namespace ContactProtocol=hp::ContactProtocol;
struct PhonebookContact{String id,name,email,phone,mobile,street,houseNumber,postalCode,city,country,notes,userId,company,jobTitle,firstName,lastName;bool readOnly=false;
ContactProtocol::Birthday birthday;std::vector<ContactProtocol::Value> phones,emails;std::vector<ContactProtocol::Address> addresses;};
'''+function((ROOT/'src/services/PersonName.h').read_text(encoding='utf-8'),'bool validPersonNamePart(')+function((ROOT/'src/services/PersonName.h').read_text(encoding='utf-8'),'void legacyPersonName(')+function(source,'bool valid(')+function(source,'PhonebookContact readContact(')+function(source,'void writeContact(')+r'''
int main(){JsonDocument old;old["id"]="stable";old["name"]="Anna";old["phone"]="123";old["mobile"]="456";old["email"]="a@test.de";old["street"]="Hauptstraße";old["houseNumber"]="12a";
auto c=readContact(old.as<JsonObjectConst>(),false);assert(valid(c)&&c.phones.size()==2&&c.emails.size()==1&&c.addresses[0].houseNumber=="12a");
c.birthday={1990,2,28};c.phones[1].label="Zentrale";JsonDocument current;writeContact(current.to<JsonObject>(),c);std::string body;serializeJson(current,body);
JsonDocument restored;assert(!deserializeJson(restored,body));auto d=readContact(restored.as<JsonObjectConst>(),false);assert(valid(d)&&d.id==c.id&&d.birthday.year==1990&&d.phones[1].label=="Zentrale");
restored["email"]="old@test.de";restored["emails"][0]["value"]="new@test.de";assert(readContact(restored.as<JsonObjectConst>(),false).email=="new@test.de");
restored["emails"].to<JsonArray>();assert(readContact(restored.as<JsonObjectConst>(),false).email.empty());
restored["addresses"].to<JsonArray>();assert(readContact(restored.as<JsonObjectConst>(),false).street.empty());
// Legacy input constructs temporary fallback strings; serialization must own them.
PhonebookContact legacy;legacy.id="legacy";legacy.name="Bob";legacy.phone="789";legacy.birthday={0,12,24};writeContact(current.to<JsonObject>(),legacy);
std::vector<std::string> churn(100,std::string(100,'x'));serializeJson(current,body);assert(!deserializeJson(restored,body));d=readContact(restored.as<JsonObjectConst>(),false);assert(d.phones.size()==1&&d.phones[0].value=="789"&&d.birthday.day==24);
restored["birthday"]=123;assert(!valid(readContact(restored.as<JsonObjectConst>(),false)));
restored["birthday"]="";restored["phones"].to<JsonArray>().add(123);assert(!valid(readContact(restored.as<JsonObjectConst>(),false)));
}
''',includes=(json_header.parent,))

if __name__=='__main__':unittest.main()

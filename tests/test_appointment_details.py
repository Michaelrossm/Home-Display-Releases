"""Host execution of detail validation plus integration/privacy boundaries."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AppointmentDetailsTests(unittest.TestCase):
    def test_native_details_snapshot_binding_and_corruption(self):
        with tempfile.TemporaryDirectory(prefix='hp-details-') as directory:
            folder = Path(directory)
            shutil.copyfile(ROOT / 'src/apps/calendar/EventDetails.h', folder / 'EventDetails.h')
            shutil.copyfile(ROOT / 'src/services/ContactProtocol.h', folder / 'ContactProtocol.h')
            shutil.copyfile(ROOT / 'src/services/ContactProtocol.cpp', folder / 'ContactProtocol.cpp')
            protocol = (folder / 'ContactProtocol.cpp').read_text(encoding='utf-8')
            protocol = protocol.replace('../apps/calendar/CalendarDate.h', (ROOT / 'src/apps/calendar/CalendarDate.h').as_posix())
            (folder / 'ContactProtocol.cpp').write_text(protocol, encoding='utf-8')
            shutil.copyfile(ROOT / 'src/services/CalDavDiscovery.h', folder / 'CalDavDiscovery.h')
            shutil.copyfile(ROOT / 'src/services/CalDavDiscovery.cpp', folder / 'CalDavDiscovery.cpp')
            source = r'''
#include "EventDetails.h"
#include "ContactProtocol.h"
#include "CalDavDiscovery.h"
#include <cassert>
#include <memory>
#include <string>
#include <ctime>
#include <cstdio>
class String {public:std::string v;String()=default;String(const char* s):v(s?s:""){}
 size_t length()const{return v.size();}void reserve(size_t n){v.reserve(n);}const char* c_str()const{return v.c_str();}
 char operator[](size_t n)const{return v[n];}String& operator+=(const char* s){v+=s;return *this;}
 String& operator+=(const String& s){v+=s.v;return *this;}String& operator+=(char c){v+=c;return *this;}};
size_t strlcpy(char* to,const char* from,size_t size){size_t n=strlen(from);if(size){memcpy(to,from,n<size-1?n:size-1);to[n<size-1?n:size-1]=0;}return n;}
#ifdef _WIN32
tm* gmtime_r(const time_t* t,tm* out){return gmtime_s(out,t)==0?out:nullptr;}
#endif
namespace hp {
constexpr uint16_t EventAllDay=1;
struct EventRecord {uint64_t localId=0;uint16_t flags=0;int64_t startUtc=1,endUtc=2;char uid[56]{},title[72]{},location[48]{},notes[96]{};};
class CalDavCore {public:String serializeEvent(const EventRecord&)const;String serializeEvent(const EventRecord&,const EventDetails&)const;
 static String escapeText(const char*);static String utcStamp(int64_t);};
'''
            core = (ROOT / 'src/services/CalDavCore.cpp').read_text(encoding='utf-8')
            source += core[core.index('String CalDavCore::escapeText'):]
            source += r'''
int main(){using namespace hp;
 auto snapshot=std::make_unique<EventDetailsSnapshot>();
 const std::string events="event-list-version-a";
 snapshot->count=1;auto& d=snapshot->items[0];d.localId=123;
 strcpy(d.street,"Mühlenstraße");strcpy(d.houseNumber,"12a");strcpy(d.city,"Köln");
 strcpy(d.postalCode,"50667");strcpy(d.phone,"+49 221 12345");
 memset(d.notes,'x',4000);d.notes[4000]=0;assert(d.valid());
 snapshot->eventsChecksum=detailChecksum(events.data(),events.size());
 snapshot->detailsChecksum=detailChecksum(snapshot->items,sizeof(EventDetails));
 assert(snapshot->valid(events.data(),events.size()));
 assert(!snapshot->valid("different",9));
 d.notes[99]='z';assert(!snapshot->valid(events.data(),events.size()));
 d.notes[99]='x';assert(snapshot->valid(events.data(),events.size()));
 d.notesHtml=2;assert(!d.valid());d.notesHtml=0;
 EventRecord event;event.localId=123;strcpy(event.uid,"test@homepanel");strcpy(event.title,"Termin");
 auto encoded=CalDavCore{}.serializeEvent(event,d).v;size_t pos=0;
 while(pos<encoded.size()){const auto end=encoded.find("\r\n",pos);assert(end!=std::string::npos);assert(end-pos<=75);pos=end+2;}
 auto unfolded=encoded;while((pos=unfolded.find("\r\n "))!=std::string::npos)unfolded.erase(pos,3);
 assert(unfolded.find("X-HOMEPANEL-STREET:Mühlenstraße")!=std::string::npos);
 assert(unfolded.find("X-HOMEPANEL-HOUSE-NUMBER:12a")!=std::string::npos);
 assert(unfolded.find("CONTACT:+49 221 12345")!=std::string::npos);
 assert(unfolded.find(std::string(4000,'x'))!=std::string::npos);
 event.localId=124;assert(CalDavCore{}.serializeEvent(event,d).length()==0);
 memset(d.street,'x',sizeof(d.street));assert(!d.valid());
 snapshot->count=33;assert(!snapshot->valid(events.data(),events.size()));
 StoredEventDetails record;record.details.localId=12;record.checksum=detailChecksum(&record.details,sizeof(record.details));
 assert(record.valid(12));assert(!record.valid(13));record.version=2;assert(!record.valid(12));
 char preview[3];detailPreview(preview,sizeof(preview),"aü");assert(std::string(preview)=="a");
 char complete[4];detailPreview(complete,sizeof(complete),"aü");assert(std::string(complete)=="aü");
 ContactProtocol::Card card;
 assert(ContactProtocol::parseVcard("BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Müller\r\nitem1.TEL;TYPE=CELL:+49 123\r\nEMAIL:a@example.com\r\nADR:;;Mühlenstraße 12a;Köln;;50667;Deutschland\r\nNOTE:Zwei\\nZeilen und \\; Zeichen\r\nEND:VCARD\r\n",card));
 assert(card.name=="Müller"&&card.mobile=="+49 123"&&card.street=="Mühlenstraße 12a"&&card.city=="Köln");
 assert(card.notes=="Zwei\nZeilen und ; Zeichen");
 assert(ContactProtocol::parseVcard("BEGIN:VCARD\nVERSION:4.0\nFN:Langer\n Name\nTEL;VALUE=uri:tel:+49123\nEND:VCARD\n",card));
 assert(card.name=="LangerName"&&card.phone=="+49123");
 assert(!ContactProtocol::parseVcard("BEGIN:VCARD\nVERSION:2.1\nFN:Alt\nEND:VCARD",card));
 assert(!ContactProtocol::parseVcard("BEGIN:VCARD\nVERSION:3.0\nFN;ENCODING=QUOTED-PRINTABLE:Alt\nEND:VCARD",card));
 auto notes=ContactProtocol::plainNotes("<p>Treffen &amp; Notiz</p><b>ü</b>",true);assert(notes.find("Treffen & Notiz")!=std::string::npos&&notes.find("<p>")==std::string::npos);
 DavDiscovery discovery;std::string xml="<d:multistatus xmlns:d='DAV:' xmlns:c='urn:ietf:params:xml:ns:carddav'><d:response><d:href>/book/</d:href><d:propstat><d:prop><c:addressbook-home-set><d:href>/home/</d:href></c:addressbook-home-set><d:resourcetype><c:addressbook/></d:resourcetype></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response></d:multistatus>";
 assert(CalDavDiscovery::parse(xml.c_str(),xml.size(),discovery,true));assert(discovery.homes.size()==1&&discovery.calendars.size()==1);
 assert(CalDavDiscovery::parse(xml.c_str(),xml.size(),discovery));assert(discovery.homes.empty()&&discovery.calendars.empty());
 xml="<d:multistatus xmlns:d='DAV:' xmlns:c='urn:ietf:params:xml:ns:carddav'><d:response><d:href>/a.vcf</d:href><d:propstat><d:prop><c:address-data><![CDATA[BEGIN:VCARD\nVERSION:3.0\nFN:Müller\nEND:VCARD]]></c:address-data></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response></d:multistatus>";
 assert(CalDavDiscovery::parse(xml.c_str(),xml.size(),discovery,true));assert(discovery.cards.size()==1&&discovery.cards[0].data.find("Müller")!=std::string::npos);
}
'''
            (folder / 'check.cpp').write_text(source, encoding='utf-8')
            compiler = shutil.which('g++') or shutil.which('clang++')
            exe = folder / ('check.exe' if os.name == 'nt' else 'check')
            if compiler:
                command = [compiler, '-std=c++17', '-I', str(folder), 'check.cpp', 'ContactProtocol.cpp', 'CalDavDiscovery.cpp', '-o', str(exe)]
            else:
                vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
                if not vswhere.exists(): self.skipTest('Host compiler missing')
                install = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
                vcvars = Path(install) / 'VC/Auxiliary/Build/vcvars64.bat'
                command = f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /utf-8 /EHsc /std:c++17 /I"{folder}" check.cpp ContactProtocol.cpp CalDavDiscovery.cpp /Fe:check.exe"'
            result = subprocess.run(command, cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_opt_in_phonebook_and_full_detail_import_boundaries(self):
        header = (ROOT / 'src/services/CalendarSyncService.h').read_text(encoding='utf-8')
        api = (ROOT / 'src/services/CloudAccountApi.cpp').read_text(encoding='utf-8')
        book = (ROOT / 'src/services/PhonebookService.cpp').read_text(encoding='utf-8')
        service = (ROOT / 'src/services/CalendarSyncService.cpp').read_text(encoding='utf-8')
        self.assertIn('bool phonebookEnabled_[5]{}', header)
        self.assertIn('doc["phonebookEnabled"].is<bool>()', api)
        self.assertIn('"phonebook-enable"', api)
        self.assertIn('!account.phonebookEnabled', book)
        self.assertIn('String(doc["email"]|"")!=account.email', book)
        self.assertIn('recoverAtomicWrite', book)
        self.assertNotIn('esp_http_client', book)
        self.assertIn('summary,description,location', service)
        self.assertIn('subject,body,start', service)
        self.assertIn('User.Read Calendars.Read"', service)
        self.assertIn('if(AccountAuthPolicy::hasContacts(purpose))scope+=', service)
        self.assertIn('copyEventDetails', service)
        copy = service[service.index('bool CalendarSyncService::copyEventDetails'):service.index('void CalendarSyncService::backgroundTick')]
        self.assertNotIn('StorageService', copy)

    def test_ui_worker_lifetime_and_separate_local_remote_storage(self):
        ui = (ROOT / 'src/ui/AccountDataUi.cpp').read_text(encoding='utf-8')
        book = (ROOT / 'src/services/PhonebookService.cpp').read_text(encoding='utf-8')
        service = (ROOT / 'src/services/CalendarSyncService.cpp').read_text(encoding='utf-8')
        self.assertIn('std::shared_ptr<Snapshot>', ui)
        self.assertIn('p->state->alive=false', ui)
        self.assertIn('NetworkWorker::instance().submit', ui)
        worker = ui[ui.index('void load(void*'):ui.index('void queue(Page&')]
        self.assertNotIn('lv_', worker)
        self.assertIn('plainNotes', worker)
        self.assertIn('current.email!=s->email', worker)
        self.assertIn('if(!old.readOnly)', ui)
        self.assertIn('if(!c.readOnly)', ui)
        self.assertIn('_remote.json', book)
        self.assertIn('(!remote&&c.readOnly)', book)
        fetch = service[service.index('bool CalendarSyncService::fetchContacts'):]
        self.assertIn('!phonebookEnabled_[job.slot]', fetch)
        self.assertIn('!current(job)', fetch)
        self.assertIn('"https://graph.microsoft.com/v1.0/me/contacts?"', fetch)
        self.assertIn('Contacts', service)

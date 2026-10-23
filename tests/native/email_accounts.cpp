#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <vector>
#include "EmailAccountService.h"
#include "CalDavDiscovery.h"
void mbedtls_platform_zeroize(void* data,size_t size){auto* p=static_cast<volatile unsigned char*>(data);while(size--)*p++=0;}
namespace hp {
class SecurityService { public:
    bool allowed=true,locked=false;
    static SecurityService& instance(){static SecurityService s;return s;}
    bool settingsAllowed(){return allowed;}bool tabletLocked(){return locked;}
};
class SecurityWriteGuard {};
class CredentialStore {
    friend class EmailAccountService;
    std::array<std::vector<unsigned char>,5> records;
    bool writeEmailRecord(uint8_t slot,const void* data,size_t size){
        if(failWrite||slot>=5)return false;
        const auto* p=static_cast<const unsigned char*>(data);records[slot].assign(p,p+size);return true;
    }
    bool readEmailRecord(uint8_t slot,void* data,size_t size){
        if(slot>=5||records[slot].size()!=size)return false;
        memcpy(data,records[slot].data(),size);return true;
    }
    bool removeEmailRecord(uint8_t slot){if(failWrite||slot>=5)return false;records[slot].clear();return true;}
    bool removeOAuthRecord(const char*){return !failWrite;}
public:
    bool failWrite=false;
    static CredentialStore& instance(){static CredentialStore s;return s;}
    void corrupt(uint8_t slot){records[slot][0]=99;}
};
class CalendarSyncService {public:
    static CalendarSyncService& instance(){static CalendarSyncService s;return s;}
    void invalidate(uint8_t){}
    bool disconnect(uint8_t){return true;}
    void accountChanged(uint8_t){}
    String calDavServer(uint8_t){return "";}
    static bool adopt(uint8_t slot,EmailProvider p,const String& mail){return EmailAccountService::instance().storeOAuthIdentity(slot,p,mail);}
    static bool read(uint8_t slot,char* address,char* password){EmailProvider provider;return EmailAccountService::instance().loadCredentials(slot,address,255,password,193,provider);}
};
}
#include "service.inc"
#include "AccountAuthTypes.h"
int main(){
    using namespace hp;auto& service=EmailAccountService::instance();
    using namespace CalDavDiscovery;
    assert(httpsUrl("https://cloud.example:8443/remote.php/dav/"));
    for(const char* bad:{"http://cloud.example/dav/","https://user@cloud.example/dav/","https://cloud.example/dav/#x","https://cloud.example:0/dav/","https://cloud.example:65536/dav/","https://cloud.example/%0d%0aHeader"})assert(!httpsUrl(bad));
    assert(resolve("https://cloud.example/","/dav/")=="https://cloud.example/dav/");
    assert(resolve("https://cloud.example","dav/")=="https://cloud.example/dav/");
    assert(resolve("https://cloud.example/dav/users/","../home/")=="https://cloud.example/dav/home/");
    assert(resolve("https://cloud.example/","//evil.example/").empty());
    assert(allowedTarget("https://cloud.example/dav/","https://cloud.example/home/",false));
    assert(!allowedTarget("https://cloud.example/dav/","https://evil.example/home/",false));
    assert(!allowedTarget("https://cloud.example/dav/","http://cloud.example/home/",false));
    assert(allowedTarget("https://caldav.icloud.com/","https://p01-caldav.icloud.com/home/",true));
    assert(!allowedTarget("https://caldav.icloud.com/","https://icloud.com.evil.example/home/",true));
    assert(!allowedTarget("https://caldav.icloud.com/","https://p01-caldav.icloud.com:8443/home/",true));
    assert(appleAppPassword("abcd-efgh-ijkl-mnop"));
    for(const char* bad:{"normal-apple-password","abcdefghijklmnop","abcd-efgh-ijkl-mno1","ABCD-EFGH-IJKL-MNOP",""})assert(!appleAppPassword(bad));
    const std::string discovery=R"XML(<?xml version="1.0"?><x:multistatus xmlns:x="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav">
    <x:response><x:href>/dav/</x:href><x:propstat><x:prop><x:current-user-principal><x:href>/principal/</x:href></x:current-user-principal><c:calendar-home-set><x:href>/home/</x:href><x:href>/shared/</x:href></c:calendar-home-set></x:prop><x:status>HTTP/1.1 200 OK</x:status></x:propstat></x:response>
    <x:response><x:href>/home/personal/</x:href><x:propstat><x:prop><x:resourcetype><x:collection/><c:calendar/></x:resourcetype><x:displayname>Privat &amp; Familie &#228;</x:displayname></x:prop><x:status>HTTP/1.1 200 OK</x:status></x:propstat></x:response>
    <x:response><x:href>/home/work/</x:href><x:propstat><x:prop><x:resourcetype><c:calendar/></x:resourcetype><x:displayname>Arbeit</x:displayname></x:prop><x:status>HTTP/1.1 200 OK</x:status></x:propstat></x:response>
    <x:response><x:href>/home/denied/</x:href><x:propstat><x:prop><x:resourcetype><c:calendar/></x:resourcetype></x:prop><x:status>HTTP/1.1 403 Forbidden</x:status></x:propstat></x:response></x:multistatus>)XML";
    DavDiscovery result;assert(parse(discovery.data(),discovery.size(),result));
    assert(result.principal=="/principal/"&&result.homes.size()==2&&result.calendars.size()==2);
    assert(result.calendars[0].name=="Privat & Familie ä");
    const std::string defaultNs=R"XML(<multistatus xmlns="DAV:"><response><href>/ok/</href><propstat><prop><resourcetype><calendar xmlns="urn:ietf:params:xml:ns:caldav"/></resourcetype></prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>)XML";
    assert(parse(defaultNs.data(),defaultNs.size(),result)&&result.calendars.size()==1);
    for(const std::string& bad:{std::string("<!DOCTYPE a [<!ENTITY x SYSTEM 'file:///etc/passwd'>]>")+discovery,discovery.substr(0,discovery.size()-15),std::string("<multistatus xmlns='evil'/>")})assert(!parse(bad.data(),bad.size(),result));
    using namespace AccountAuthPolicy;
    assert(deviceFlow(EmailProvider::Microsoft,OAuthPurpose::Identity));
    assert(deviceFlow(EmailProvider::Microsoft,OAuthPurpose::Calendar));
    assert(deviceFlow(EmailProvider::Google,OAuthPurpose::Identity));
    assert(!deviceFlow(EmailProvider::Google,OAuthPurpose::Calendar));
    assert(resolveClientId("standard","custom",false)=="standard");
    assert(resolveClientId("standard","custom",true)=="custom");
    assert(resolveClientId("","custom",false)=="");
    assert(validClientId("abc-123.apps.googleusercontent.com"));
    assert(!validClientId(""));assert(!validClientId("id\r\n"));
    assert(active(AccountAuthState::RequestingCode));assert(active(AccountAuthState::AwaitingUser));assert(active(AccountAuthState::Polling));
    for(auto s:{AccountAuthState::Idle,AccountAuthState::Connected,AccountAuthState::Expired,AccountAuthState::Cancelled,AccountAuthState::Error})assert(!active(s));
    assert(current(7,7,1000,999));assert(!current(8,7,1000,999));assert(!current(7,7,1000,1000));
    assert(current(7,7,100,0xfffffff0u));assert(!current(7,7,0xfffffff0u,100));
    assert(remaining(2001,1000)==2);assert(remaining(1000,1000)==0);assert(remaining(0,1000)==0);
    assert(!strcmp(stateName(AccountAuthState::Cancelled),"Cancelled"));
    for(uint8_t i=0;i<5;++i)assert(!service.account(i).configured);
    assert(!service.save(5,EmailProvider::Google,"a@example.com","secret"));
    assert(!service.save(0,EmailProvider(255),"a@example.com","secret"));
    for(const char* invalid:{"", "a", "a@", "@example.com", "a@@example.com", "a@domain", "a b@example.com", "a@exam\rple.com"}){
        assert(!service.save(0,EmailProvider::Google,invalid,"secret"));
    }
    assert(service.save(0,EmailProvider::Google,"a@example.com",""));
    assert(service.save(0,EmailProvider::Google," User@EXAMPLE.COM ","correct-secret"));
    assert(service.account(0).address=="User@example.com");
    assert(service.save(0,EmailProvider::Google,"User@example.com",""));
    char address[255]{},password[193]{};
    assert(CalendarSyncService::read(0,address,password));assert(!strcmp(password,"correct-secret"));
    assert(service.save(0,EmailProvider::Google,"other@example.com",""));
    assert(service.save(0,EmailProvider::Google,"User@example.com","correct-secret"));
    assert(!service.save(0,EmailProvider::Apple,"User@example.com",""));
    std::string tooLong(193,'x');assert(!service.save(0,EmailProvider::Google,"User@example.com",tooLong.c_str()));
    for(uint8_t i=1;i<5;++i)assert(service.save(i,EmailProvider::Gmx,(std::string("user")+std::to_string(i)+"@example.com").c_str(),"slot-secret"));
    CredentialStore::instance().failWrite=true;
    assert(!service.save(0,EmailProvider::Apple,"other@example.com","replacement"));
    assert(service.account(0).address=="User@example.com");
    assert(!service.remove(0));CredentialStore::instance().failWrite=false;
    SecurityService::instance().allowed=false;
    assert(!service.save(0,EmailProvider::Google,"User@example.com","replacement"));assert(!service.remove(0));
    SecurityService::instance().allowed=true;SecurityService::instance().locked=true;assert(!service.remove(0));
    SecurityService::instance().locked=false;
    CredentialStore::instance().corrupt(1);assert(!service.account(1).configured);
    memset(address,'x',sizeof(address));memset(password,'x',sizeof(password));
    assert(!CalendarSyncService::read(1,address,password));
    for(char c:address)assert(c==0);for(char c:password)assert(c==0);
    assert(service.remove(0));assert(!service.account(0).configured);
    assert(CalendarSyncService::adopt(0,EmailProvider::Microsoft,"oauth@example.com"));
    assert(service.account(0).oauthManaged&&service.account(0).provider==EmailProvider::Microsoft);
    assert(CalendarSyncService::read(0,address,password));assert(!strcmp(password,""));
    assert(CalendarSyncService::adopt(0,EmailProvider::Microsoft,"replacement@example.com"));
    assert(service.account(0).address=="replacement@example.com");
    SecurityService::instance().allowed=false;
    assert(!CalendarSyncService::adopt(0,EmailProvider::Microsoft,"blocked@example.com"));
    SecurityService::instance().allowed=true;
    assert(!CalendarSyncService::adopt(2,EmailProvider::Microsoft,"overwrite@example.com"));
    assert(!CalendarSyncService::adopt(0,EmailProvider::Gmx,"invalid@example.com"));
    for(uint8_t i=2;i<5;++i)assert(service.account(i).configured);
}

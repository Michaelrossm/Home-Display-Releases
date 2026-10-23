#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <memory>
#include <new>
struct String:std::string {
    using std::string::string;using std::string::operator=;
    String(const std::string& s):std::string(s){}
    String(int n):std::string(std::to_string(n)){}
    String(unsigned n):std::string(std::to_string(n)){}
    String(double n,int digits){char b[64];snprintf(b,sizeof(b),"%.*f",digits,n);assign(b);}
    size_t length()const{return size();}
    int indexOf(const char* s)const{auto n=find(s);return n==npos?-1:int(n);}
    int indexOf(char s)const{auto n=find(s);return n==npos?-1:int(n);}
    bool startsWith(const char* s)const{return rfind(s,0)==0;}
    bool startsWith(const String& s)const{return rfind(s,0)==0;}
    bool endsWith(const char* s)const{const size_t n=strlen(s);return size()>=n&&compare(size()-n,n,s)==0;}
    int lastIndexOf(char c)const{auto n=rfind(c);return n==npos?-1:int(n);}
    String substring(size_t first)const{return substr(first);}
    String substring(size_t first,size_t end)const{return substr(first,end-first);}
    void remove(size_t first,size_t count){erase(first,count);}
    void replace(const char* from,const char* to){size_t at=0;while((at=find(from,at))!=npos){std::string::replace(at,strlen(from),to);at+=strlen(to);}}
    void trim(){auto a=find_first_not_of(" \t\r\n");auto b=find_last_not_of(" \t\r\n");assign(a==npos?"":substr(a,b-a+1));}
    void toUpperCase(){for(auto& c:*this)c=static_cast<char>(toupper(static_cast<unsigned char>(c)));}
    void toLowerCase(){for(auto& c:*this)c=static_cast<char>(tolower(static_cast<unsigned char>(c)));}
    size_t write(unsigned char c){push_back(c);return 1;}
    size_t write(const unsigned char* s,size_t n){append(reinterpret_cast<const char*>(s),n);return n;}
};
String operator+(const String& s,int n){return String(std::string(s)+std::to_string(n));}
String operator+(const String& s,char c){return String(std::string(s)+c);}
uint32_t now=100;uint32_t millis(){return now;}
size_t strlcpy(char* out,const char* in,size_t size){size_t n=strlen(in);if(size){memcpy(out,in,std::min(n,size-1));out[std::min(n,size-1)]=0;}return n;}
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
void* heap_caps_malloc(size_t n,int){return malloc(n);}
void* heap_caps_realloc(void* p,size_t n,int){return realloc(p,n);}
void heap_caps_free(void* p){free(p);}
#include <ArduinoJson.h>
namespace ArduinoJson {
template<>struct Converter<String> {
    static String fromJson(JsonVariantConst v){return v.is<const char*>()?String(v.as<const char*>()):String();}
    static void toJson(const String& s,JsonVariant v){v.set(std::string(s));}
    static bool checkJson(JsonVariantConst v){return v.is<const char*>();}
};
}
namespace hp {
enum class AppPermission:uint32_t{Network=1,UserStorage=4,StorageRead=8,Secrets=64};
using AppPermissions=uint32_t;uint32_t perm(AppPermission p){return uint32_t(p);}
class LuaJobService {
public:
    struct Result{bool ok=false;int status=0;String body,error;uint32_t queuedAt=now;};
    struct Request;using Handler=void(*)(const Request&,Result&);
    struct Header{String name,value;};
    struct Request {
        String owner,url,fileName,body,contentType,context;AppPermissions permissions=0,required=0;
        uint8_t kind=0,method=0;uint32_t resource=0,timeout=0,offset=0;size_t responseLimit=16384,headerCount=0;
        Header headers[8];const std::atomic<bool>* cancelled=nullptr;Handler handler=nullptr;const char* completion=nullptr;
        void(*progress)(void*,uint64_t,uint64_t)=nullptr;void* progressContext=nullptr;
        void(*discardResult)(const char*,const String&)=nullptr;
        size_t expected=0;uint8_t streamMode=0;uint32_t deadlineMs=0;
        size_t(*nativeRead)(void*,uint64_t,uint8_t*,size_t)=nullptr;void* nativeReadContext=nullptr;
    };
    Request queued;static LuaJobService& instance(){static LuaJobService s;return s;}
    uint32_t submit(const Request& r){queued=r;return 42;}
    static void executeHttp(const Request&,Result&);
};
}

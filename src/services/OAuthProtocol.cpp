#include "OAuthProtocol.h"
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>
#include <cstring>
namespace hp::OAuthProtocol {
String encode(const String& input){String result;const char* hex="0123456789ABCDEF";
    for(size_t i=0;i<input.length();++i){const uint8_t c=input[i];if(isalnum(c)||c=='-'||c=='.'||c=='_'||c=='~')result+=char(c);
        else{result+='%';result+=hex[c>>4];result+=hex[c&15];}}return result;}
bool httpsRedirect(const String& url){
    if(!url.startsWith("https://")||url.length()>254||url.indexOf('?')>=0||url.indexOf('#')>=0||url.indexOf('@')>=0||url.indexOf('\\')>=0)return false;
    for(size_t i=0;i<url.length();++i)if(uint8_t(url[i])<=32||uint8_t(url[i])>=127)return false;
    const int slash=url.indexOf('/',8);if(slash<0||url.substring(slash)!="/oauth/callback")return false;
    String host=url.substring(8,slash);host.toLowerCase();
    if(host=="localhost"||host.endsWith(".local")||host.indexOf(':')>=0||host.indexOf('.')<=0)return false;
    size_t label=0;
    for(size_t i=0;i<host.length();++i){const char c=host[i];
        if(c=='.'){if(!label||host[i-1]=='-')return false;label=0;continue;}
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')||(!label&&c=='-')||++label>63)return false;
    }
    if(!label||host[host.length()-1]=='-')return false;
    bool numeric=true;for(size_t i=0;i<host.length();++i)if(host[i]!='.'&&!isdigit(host[i]))numeric=false;
    return !numeric;
}
bool constantEqual(const String& a,const String& b){size_t diff=a.length()^b.length();for(size_t i=0;i<a.length();++i)diff|=uint8_t(a[i])^(i<b.length()?uint8_t(b[i]):0);return diff==0;}
bool callbackAllowed(const String& expected,const String& received,uint32_t expires,uint32_t now,bool consumed,bool pending){
    return expected.length()==64&&received.length()==64&&expires&&int32_t(now-expires)<0&&!consumed&&!pending&&constantEqual(expected,received);
}
uint32_t pollDelay(uint32_t interval,bool slowDown){
    if(interval<1000)interval=1000;if(interval>60000)interval=60000;
    return slowDown?(interval>=55000?60000:interval+5000):interval;
}
String randomToken(){uint8_t bytes[32];esp_fill_random(bytes,sizeof(bytes));char hex[65];for(size_t i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",bytes[i]);
    String result(hex);mbedtls_platform_zeroize(bytes,sizeof(bytes));mbedtls_platform_zeroize(hex,sizeof(hex));return result;}
String challenge(const String& input){unsigned char digest[32],encoded[48];size_t count=0;
    if(mbedtls_sha256(reinterpret_cast<const uint8_t*>(input.c_str()),input.length(),digest,0)||mbedtls_base64_encode(encoded,sizeof(encoded),&count,digest,sizeof(digest)))return "";
    String result(reinterpret_cast<char*>(encoded),count);result.replace("+","-");result.replace("/","_");result.replace("=","");mbedtls_platform_zeroize(digest,sizeof(digest));return result;}
int64_t utcDate(const char* value){
    if(!value)return 0;const size_t length=strlen(value);if(length<10)return 0;
    auto number=[&](size_t at,size_t n)->int{
        if(at+n>length)return -1;int out=0;for(size_t i=at;i<at+n;++i){
            if(value[i]<'0'||value[i]>'9')return -1;out=out*10+value[i]-'0';
        }return out;
    };
    const int year=number(0,4),month=number(5,2),day=number(8,2);
    if(value[4]!='-'||value[7]!='-'||year<1970||year>2100||month<1||month>12||day<1)return 0;
    const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap=year%4==0&&(year%100!=0||year%400==0);
    if(day>days[month-1]+(month==2&&leap))return 0;
    int hour=0,minute=0,second=0,offset=0;
    if(length>10){
        if(length<19||value[10]!='T'||value[13]!=':'||value[16]!=':')return 0;
        hour=number(11,2);minute=number(14,2);second=number(17,2);
        if(hour<0||hour>23||minute<0||minute>59||second<0||second>59)return 0;
        size_t at=19;
        if(at<length&&value[at]=='.'){const size_t start=++at;while(at<length&&isdigit(uint8_t(value[at])))++at;if(at==start)return 0;}
        if(at<length&&value[at]=='Z'){if(++at!=length)return 0;}
        else if(at<length&&(value[at]=='+'||value[at]=='-')){
            if(length-at!=6||value[at+3]!=':')return 0;const int h=number(at+1,2),m=number(at+4,2);
            if(h<0||h>23||m<0||m>59)return 0;offset=(value[at]=='+'?1:-1)*(h*3600+m*60);at=length;
        }
        if(at!=length)return 0; // Graph UTC responses may omit the suffix.
    }
    const int y=year-(month<=2),era=y/400;const unsigned yoe=y-era*400;
    const unsigned doy=(153*(month+(month>2?-3:9))+2)/5+day-1,doe=yoe*365+yoe/4-yoe/100+doy;
    return (int64_t(era)*146097+doe-719468)*86400+hour*3600+minute*60+second-offset;
}
String utcText(int64_t value){time_t epoch=value;tm date{};gmtime_r(&epoch,&date);char text[24];strftime(text,sizeof(text),"%Y-%m-%dT%H:%M:%SZ",&date);return text;}
}

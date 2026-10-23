#include "CalDavDiscovery.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
namespace hp { namespace CalDavDiscovery {
namespace {
struct Node {std::string qname,name,ns,text;int parent=-1;};
bool decode(const std::string& in,std::string& out){
 out.clear();for(size_t i=0;i<in.size();++i){unsigned char c=in[i];if(c<32&&c!=9&&c!=10&&c!=13)return false;
 if(c!='&'){out+=char(c);continue;}auto end=in.find(';',i+1);if(end==std::string::npos||end-i>12)return false;
 const auto e=in.substr(i+1,end-i-1);if(e=="amp")out+='&';else if(e=="lt")out+='<';else if(e=="gt")out+='>';else if(e=="quot")out+='"';else if(e=="apos")out+='\'';
 else if(e.size()>1&&e[0]=='#'){char* last=nullptr;const bool hex=e.size()>2&&e[1]=='x';const auto n=strtoul(e.c_str()+(hex?2:1),&last,hex?16:10);
 if(!last||*last||!n||n>0x10ffff||(n>=0xd800&&n<=0xdfff)||(n<32&&n!=9&&n!=10&&n!=13))return false;
 if(n<128)out+=char(n);else if(n<2048){out+=char(0xc0|(n>>6));out+=char(0x80|(n&63));}else if(n<65536){out+=char(0xe0|(n>>12));out+=char(0x80|((n>>6)&63));out+=char(0x80|(n&63));}else{out+=char(0xf0|(n>>18));out+=char(0x80|((n>>12)&63));out+=char(0x80|((n>>6)&63));out+=char(0x80|(n&63));}}
 else return false;i=end;}return true;
}
std::string trim(std::string s){auto a=s.find_first_not_of(" \r\n\t"),b=s.find_last_not_of(" \r\n\t");return a==std::string::npos?"":s.substr(a,b-a+1);}
bool nameChar(char c){return std::isalnum(static_cast<unsigned char>(c))||c=='_'||c==':'||c=='-'||c=='.';}
int child(const std::vector<Node>& nodes,int parent,const char* ns,const char* name){for(size_t i=0;i<nodes.size();++i)if(nodes[i].parent==parent&&nodes[i].ns==ns&&nodes[i].name==name)return int(i);return -1;}
std::string text(const std::vector<Node>& n,int i){return i<0?"":trim(n[i].text);}
std::string hostname(const std::string& url){auto o=origin(url);return o.empty()?"":o.substr(8,o.find(':',8)==std::string::npos?std::string::npos:o.find(':',8)-8);}
bool appleHost(const std::string& h){return h=="caldav.icloud.com"||(h.size()>11&&h.compare(h.size()-11,11,".icloud.com")==0);}
}
bool parse(const char* input,size_t length,DavDiscovery& result,bool contacts){
 result={};if(!input||!length||length>65536)return false;std::string xml(input,length);std::vector<Node> nodes;std::vector<int> stack;std::vector<std::map<std::string,std::string>> contexts;
 size_t p=0;unsigned roots=0;while(p<xml.size()){
 if(xml[p]!='<'){auto end=xml.find('<',p);if(end==std::string::npos)end=xml.size();std::string value;if(!decode(xml.substr(p,end-p),value))return false;
 if(stack.empty()){if(!trim(value).empty())return false;}else nodes[stack.back()].text+=value;p=end;continue;}
 if(xml.compare(p,4,"<!--")==0){auto end=xml.find("-->",p+4);if(end==std::string::npos)return false;p=end+3;continue;}
 if(xml.compare(p,2,"<?")==0){auto end=xml.find("?>",p+2);if(end==std::string::npos)return false;p=end+2;continue;}
 if(xml.compare(p,9,"<![CDATA[")==0){if(stack.empty())return false;auto end=xml.find("]]>",p+9);if(end==std::string::npos)return false;nodes[stack.back()].text+=xml.substr(p+9,end-p-9);p=end+3;continue;}
 if(xml.compare(p,2,"<!")==0)return false; // No DTD, external entities or expansion.
 ++p;const bool closing=p<xml.size()&&xml[p]=='/';if(closing)++p;auto start=p;while(p<xml.size()&&nameChar(xml[p]))++p;
 auto qname=xml.substr(start,p-start);if(qname.empty()||qname.size()>128)return false;
 auto ws=[&](){while(p<xml.size()&&std::isspace(static_cast<unsigned char>(xml[p])))++p;};ws();
 if(closing){if(p>=xml.size()||xml[p]!='>'||stack.empty()||nodes[stack.back()].qname!=qname)return false;stack.pop_back();contexts.pop_back();++p;continue;}
 Node node;node.qname=qname;std::map<std::string,std::string> bindings;if(!stack.empty()){node.parent=stack.back();bindings=contexts.back();}else if(++roots!=1)return false;
 std::vector<std::string> attributes;while(p<xml.size()&&xml[p]!='>'&&xml[p]!='/'){
 start=p;while(p<xml.size()&&nameChar(xml[p]))++p;auto attr=xml.substr(start,p-start);if(attr.empty()||attributes.size()>=24||std::find(attributes.begin(),attributes.end(),attr)!=attributes.end())return false;attributes.push_back(attr);ws();
 if(p>=xml.size()||xml[p++]!='=')return false;ws();if(p>=xml.size()||(xml[p]!='\''&&xml[p]!='"'))return false;char quote=xml[p++];auto end=xml.find(quote,p);if(end==std::string::npos)return false;
 std::string value;if(!decode(xml.substr(p,end-p),value)||value.size()>2048)return false;
 if(attr=="xmlns")bindings[""]=value;else if(attr.compare(0,6,"xmlns:")==0)bindings[attr.substr(6)]=value;p=end+1;ws();}
 bool self=p<xml.size()&&xml[p]=='/';if(self)++p;if(p>=xml.size()||xml[p++]!='>'||nodes.size()>=1024||stack.size()>=24||bindings.size()>16)return false;
 auto colon=qname.find(':');node.name=colon==std::string::npos?qname:qname.substr(colon+1);auto prefix=colon==std::string::npos?"":qname.substr(0,colon);
 if(colon!=std::string::npos&&!bindings.count(prefix))return false;node.ns=bindings[prefix];nodes.push_back(std::move(node));if(!self){stack.push_back(int(nodes.size()-1));contexts.push_back(std::move(bindings));}
 }
 if(!stack.empty()||nodes.empty()||nodes[0].ns!="DAV:"||nodes[0].name!="multistatus")return false;
 for(size_t r=1;r<nodes.size();++r){if(nodes[r].parent!=0||nodes[r].ns!="DAV:"||nodes[r].name!="response")continue;
 auto href=text(nodes,child(nodes,r,"DAV:","href"));std::string display;bool calendar=false;
 {auto status=text(nodes,child(nodes,r,"DAV:","status"));if(status.size()>=12&&status[9]>='4'&&status[9]<='5')result.truncated=true;}
 const char* domain=contacts?"urn:ietf:params:xml:ns:carddav":"urn:ietf:params:xml:ns:caldav";
 for(size_t ps=r+1;ps<nodes.size();++ps){if(nodes[ps].parent!=int(r)||nodes[ps].ns!="DAV:"||nodes[ps].name!="propstat")continue;
 auto status=text(nodes,child(nodes,ps,"DAV:","status"));if(status.compare(0,9,"HTTP/1.1 ")&&status.compare(0,9,"HTTP/1.0 "))continue;
 if(status.size()<12||status.substr(9,3)!="200"||(status.size()>12&&status[12]!=' ')){
  int prop=child(nodes,ps,"DAV:","prop");if(prop>=0&&child(nodes,prop,domain,contacts?"address-data":"calendar-data")>=0)result.truncated=true;
  continue;
 }
 int prop=child(nodes,ps,"DAV:","prop");if(prop<0)continue;
 int principal=child(nodes,prop,"DAV:","current-user-principal");if(principal>=0){auto value=text(nodes,child(nodes,principal,"DAV:","href"));if(result.principal.empty())result.principal=value;}
 int home=child(nodes,prop,domain,contacts?"addressbook-home-set":"calendar-home-set");
 if(home>=0)for(size_t h=0;h<nodes.size();++h)if(nodes[h].parent==home&&nodes[h].ns=="DAV:"&&nodes[h].name=="href"){
 auto value=text(nodes,h);if(!value.empty()&&std::find(result.homes.begin(),result.homes.end(),value)==result.homes.end()){if(result.homes.size()<8)result.homes.push_back(value);else result.truncated=true;}}
 int type=child(nodes,prop,"DAV:","resourcetype");if(type>=0&&child(nodes,type,domain,contacts?"addressbook":"calendar")>=0)calendar=true;
 if(contacts){int data=child(nodes,prop,domain,"address-data");if(data>=0&&!href.empty()){
     if(nodes[data].text.size()>16384||href.size()>2048)return false;
     if(result.cards.size()<128)result.cards.push_back({href,nodes[data].text});else result.truncated=true;}}
 else {int data=child(nodes,prop,domain,"calendar-data");if(data>=0&&!href.empty()){
     if(nodes[data].text.size()>65536||href.size()>2048)return false;
     if(result.resources.size()<128)result.resources.push_back({href,nodes[data].text});else result.truncated=true;}}
 int name=child(nodes,prop,"DAV:","displayname");if(name>=0)display=text(nodes,name);
 }
 if(calendar&&!href.empty()){if(href.size()>2048||display.size()>192)return false;if(result.calendars.size()<16)result.calendars.push_back({href,display});else result.truncated=true;}
 }return true;
}
bool httpsUrl(const std::string& url){
 if(url.size()<9||url.size()>2048||url.compare(0,8,"https://"))return false;
 for(unsigned char c:url)if(c<=32||c>=127||c=='\\'||c=='#'||c=='?')return false;
 auto end=url.find('/',8);auto host=url.substr(8,end==std::string::npos?std::string::npos:end-8);if(host.empty())return false;
 auto colon=host.find(':');if(colon!=std::string::npos){auto port=host.substr(colon+1);if(port.empty()||port.size()>5||port.find_first_not_of("0123456789")!=std::string::npos||strtoul(port.c_str(),nullptr,10)<1||strtoul(port.c_str(),nullptr,10)>65535)return false;host.resize(colon);}
 if(host.empty()||host.front()=='.'||host.back()=='.')return false;
 for(char c:host)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='-'&&c!='.')return false;
 std::string lower=url;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
 for(const char* bad:{"%0d","%0a","%00","%2e"})if(lower.find(bad)!=std::string::npos)return false;return true;
}
std::string origin(const std::string& url){if(!httpsUrl(url))return "";auto end=url.find('/',8);auto value=url.substr(0,end);std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});return value;}
std::string resolve(const std::string& base,const std::string& href){
 if(!httpsUrl(base)||href.empty())return "";if(href.compare(0,8,"https://")==0)return httpsUrl(href)?href:"";
 if(href.compare(0,2,"//")==0||href.find(':')!=std::string::npos)return "";
 auto root=origin(base);const auto slash=base.rfind('/');std::string directory=slash<root.size()?"/":base.substr(root.size(),slash-root.size()+1);
 std::string path=href[0]=='/'?href:directory+href;
 std::vector<std::string> parts;size_t at=0;while(at<path.size()){auto end=path.find('/',at);if(end==std::string::npos)end=path.size();auto part=path.substr(at,end-at);if(part==".."){if(parts.empty())return "";parts.pop_back();}else if(!part.empty()&&part!=".")parts.push_back(part);at=end+1;}
 std::string target=root+"/";for(size_t i=0;i<parts.size();++i){if(i)target+='/';target+=parts[i];}if(path.back()=='/'&&target.back()!='/')target+='/';return httpsUrl(target)?target:"";
}
bool allowedTarget(const std::string& root,const std::string& target,bool icloud){
 if(!httpsUrl(root)||!httpsUrl(target))return false;
 if(icloud)return appleHost(hostname(root))&&appleHost(hostname(target))&&(origin(target)=="https://"+hostname(target)||origin(target)=="https://"+hostname(target)+":443");
 return origin(root)==origin(target);
}
bool appleAppPassword(const char* value){if(!value)return false;std::string password=value;if(password.size()!=19)return false;for(size_t i=0;i<19;++i)if(i==4||i==9||i==14){if(password[i]!='-')return false;}else if(password[i]<'a'||password[i]>'z')return false;return true;}
} }

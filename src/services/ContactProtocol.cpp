#include "ContactProtocol.h"
#include <vector>
#include <cctype>
#include <cstdio>
#include <algorithm>
#include "../apps/calendar/CalendarDate.h"
namespace hp::ContactProtocol {namespace {
std::string upper(std::string value){for(char& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;}
bool validUtf8(const std::string& input){
 for(size_t i=0;i<input.size();){const uint8_t c=uint8_t(input[i++]);if(c<128){if(c<32&&c!=9&&c!=10&&c!=13)return false;continue;}
   unsigned count=0;uint32_t value=0,minimum=0;
   if(c>=0xc2&&c<=0xdf){count=1;value=c&31;minimum=0x80;}else if(c>=0xe0&&c<=0xef){count=2;value=c&15;minimum=0x800;}else if(c>=0xf0&&c<=0xf4){count=3;value=c&7;minimum=0x10000;}else return false;
   while(count--){if(i>=input.size()||(uint8_t(input[i])&0xc0)!=0x80)return false;value=(value<<6)|(uint8_t(input[i++])&63);}
   if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
 }return true;
}
std::string unescape(const std::string& value){std::string out;for(size_t i=0;i<value.size();++i){char c=value[i];if(c=='\\'&&i+1<value.size()){c=value[++i];if(c=='n'||c=='N')c='\n';}out+=c;}return out;}
std::vector<std::string> parts(const std::string& value){std::vector<std::string> out;std::string field;bool escape=false;
 for(char c:value){if(c==';'&&!escape){out.push_back(unescape(field));field.clear();}else field+=c;if(c=='\\')escape=!escape;else escape=false;}out.push_back(unescape(field));return out;}
std::string typeLabel(const std::string& name){
 if(name.find("CELL")!=std::string::npos)return "Mobil";
 if(name.find("WORK")!=std::string::npos)return "Arbeit";
 if(name.find("HOME")!=std::string::npos)return "Privat";
 if(name.find("FAX")!=std::string::npos)return "Fax";
 return "Sonstige";
}
std::string escapeText(const std::string& value){std::string out;for(char c:value){if(c=='\r')continue;if(c=='\n'){out+="\\n";continue;}if(c=='\\'||c==';'||c==',')out+='\\';out+=c;}return out;}
void appendLine(std::string& out,const std::string& line){size_t at=0;bool continuation=false;
 while(at<line.size()){size_t n=std::min<size_t>(continuation?74:75,line.size()-at);
  while(at+n<line.size()&&n&&(uint8_t(line[at+n])&0xc0)==0x80)--n;
  if(continuation)out+=' ';out.append(line,at,n);out+="\r\n";at+=n;continuation=true;
 }
}
}
bool validBirthday(Birthday b){return (!b.year&&!b.month&&!b.day)||calendar::validDate(b.year?b.year:2000,b.month,b.day);}
bool parseBirthday(const std::string& value,Birthday& out){
 if(value.empty()){out={};return true;}std::string digits;bool partial=value.rfind("--",0)==0;
 if(partial){if(value.size()!=6&&value.size()!=7)return false;if(value.size()==7&&value[4]!='-')return false;}
 else if(value.size()!=8&&(value.size()!=10||value[4]!='-'||value[7]!='-'))return false;
 for(size_t i=partial?2:0;i<value.size();++i){const char c=value[i];if(c=='-')continue;if(c<'0'||c>'9')return false;digits+=c;}
 if(digits.size()!=(partial?4:8))return false;Birthday b;
 auto number=[&](size_t at,size_t count){unsigned n=0;while(count--)n=n*10+digits[at++]-'0';return n;};
 b.year=partial?0:number(0,4);const size_t at=partial?0:4;b.month=number(at,2);b.day=number(at+2,2);
 // Accept the editor's explicit unknown year, but never an empty date.
 if(!b.month||!b.day||!validBirthday(b))return false;out=b;return true;
}
std::string birthdayText(Birthday b){if(!b.month||!validBirthday(b))return {};char text[12];
 if(b.year)snprintf(text,sizeof(text),"%04u-%02u-%02u",b.year,b.month,b.day);else snprintf(text,sizeof(text),"--%02u-%02u",b.month,b.day);return text;}
bool parseVcard(const std::string& input,Card& output){
 output={};if(input.empty()||input.size()>16384||input.find('\0')!=std::string::npos)return false;
 if(!validUtf8(input))return false;
 std::vector<std::string> lines;size_t at=0;while(at<input.size()){auto end=input.find('\n',at);if(end==std::string::npos)end=input.size();auto line=input.substr(at,end-at);if(!line.empty()&&line.back()=='\r')line.pop_back();
 if(!line.empty()&&(line[0]==' '||line[0]=='\t')){if(lines.empty())return false;lines.back()+=line.substr(1);}else if(!line.empty())lines.push_back(std::move(line));at=end+1;}
 if(lines.size()<3||upper(lines.front())!="BEGIN:VCARD"||upper(lines.back())!="END:VCARD")return false;bool version=false;
 struct Group {std::string id;unsigned kind;size_t index;};std::vector<Group> groups;
 for(size_t i=1;i+1<lines.size();++i){const auto& line=lines[i];size_t colon=std::string::npos;bool quoted=false,escape=false;
 for(size_t n=0;n<line.size();++n){if(line[n]=='"'&&!escape)quoted=!quoted;if(line[n]==':'&&!quoted){colon=n;break;}if(line[n]=='\\')escape=!escape;else escape=false;}
 if(colon==std::string::npos)return false;auto name=upper(line.substr(0,colon));const auto semi=name.find(';');auto key=name.substr(0,semi);const auto dot=key.rfind('.');if(dot!=std::string::npos)key=key.substr(dot+1);
 const auto raw=line.substr(colon+1),value=unescape(raw);
 if(name.find("ENCODING=")!=std::string::npos||(name.find("CHARSET=")!=std::string::npos&&name.find("CHARSET=UTF-8")==std::string::npos&&name.find("CHARSET=\"UTF-8\"")==std::string::npos))return false;
 if(key=="VERSION"){version=value=="3.0"||value=="4.0";if(!version)return false;}
 else if(key=="UID")output.uid=value;
 else if(key=="FN")output.name=value;
  else if(key=="N"){auto fields=parts(raw);if(fields.size()>1){output.firstName=fields[1];output.lastName=fields[0];if(output.name.empty())output.name=fields[1]+" "+fields[0];}}
 else if(key=="ORG"){auto fields=parts(raw);output.company=fields[0];}
 else if(key=="TITLE")output.jobTitle=value;
 else if(key=="BDAY"){if(!parseBirthday(value,output.birthday))return false;}
 else if(key=="EMAIL"){
  if(output.emails.size()>=8||value.size()>254)return false;
  if(output.email.empty())output.email=value;output.emails.push_back({typeLabel(name),value});
  if(dot!=std::string::npos)groups.push_back({name.substr(0,dot),1,output.emails.size()-1});
 }
 else if(key=="TEL"){auto phone=value.rfind("tel:",0)==0?value.substr(4):value;
 if(output.phones.size()>=8||phone.size()>96)return false;output.phones.push_back({typeLabel(name),phone});
 if(dot!=std::string::npos)groups.push_back({name.substr(0,dot),0,output.phones.size()-1});
 if(name.find("CELL")!=std::string::npos&&output.mobile.empty())output.mobile=phone;else if(output.phone.empty())output.phone=phone;}
 else if(key=="ADR"){auto fields=parts(raw);if(fields.size()<7||output.addresses.size()>=4)return false;
  output.addresses.push_back({typeLabel(name),fields[2],"",fields[5],fields[3],fields[4],fields[6]});
  if(dot!=std::string::npos)groups.push_back({name.substr(0,dot),2,output.addresses.size()-1});
  if(output.street.empty()){output.street=fields[2];output.city=fields[3];output.postalCode=fields[5];output.country=fields[6];}}
 else if(key=="NOTE")output.notes=value;
 else if(key=="BEGIN"||key=="END")return false;
 }
 // Apple grouped custom labels can precede or follow the corresponding property.
 for(const auto& line:lines){const auto colon=line.find(':');if(colon==std::string::npos)continue;const auto key=upper(line.substr(0,colon));const auto dot=key.find('.');
  if(dot==std::string::npos||key.substr(dot+1)!="X-ABLABEL")continue;auto label=unescape(line.substr(colon+1));
  if(label.rfind("_$!<",0)==0&&label.size()>8&&label.substr(label.size()-4)==">!$_")label=label.substr(4,label.size()-8);
  if(label.size()>48||label.find('\n')!=std::string::npos)return false;
  for(const auto& group:groups)if(group.id==key.substr(0,dot)){if(group.kind==0)output.phones[group.index].label=label;else if(group.kind==1)output.emails[group.index].label=label;else output.addresses[group.index].label=label;}
 }
 return version&&(!output.name.empty()||!output.phone.empty()||!output.mobile.empty()||!output.email.empty());
}
bool serializeVcard(const Card& c,std::string& output){
 output.clear();if(c.name.empty()||c.name.size()>256||c.notes.size()>1024||c.company.size()>192||c.jobTitle.size()>128||
  c.phones.size()>8||c.emails.size()>8||c.addresses.size()>4||!validBirthday(c.birthday))return false;
 for(const auto* value:{&c.uid,&c.name,&c.notes,&c.company,&c.jobTitle})if(!validUtf8(*value))return false;
 for(const auto& v:c.phones)if(!validUtf8(v.label)||!validUtf8(v.value))return false;
 for(const auto& v:c.emails)if(!validUtf8(v.label)||!validUtf8(v.value))return false;
 for(const auto& a:c.addresses)for(const auto* value:{&a.label,&a.street,&a.houseNumber,&a.postalCode,&a.city,&a.region,&a.country})if(!validUtf8(*value))return false;
 std::string result;auto line=[&](const std::string& s){appendLine(result,s);};
  line("BEGIN:VCARD");line("VERSION:4.0");line("FN:"+escapeText(c.name));if(!c.uid.empty())line("UID:"+escapeText(c.uid));
  if(!c.firstName.empty()||!c.lastName.empty())line("N:"+escapeText(c.lastName)+";"+escapeText(c.firstName)+";;;");
 if(!c.company.empty())line("ORG:"+escapeText(c.company));if(!c.jobTitle.empty())line("TITLE:"+escapeText(c.jobTitle));
 if(c.birthday.month)line("BDAY:"+birthdayText(c.birthday));if(!c.notes.empty())line("NOTE:"+escapeText(c.notes));
 unsigned group=0;auto property=[&](const std::string& key,const std::string& label,const std::string& value){
  const auto prefix="item"+std::to_string(++group)+".";const auto u=upper(label);const bool phone=key.rfind("TEL",0)==0;
  std::string type=u=="PRIVAT"?"home":u=="ARBEIT"?"work":phone&&u=="MOBIL"?"cell":phone&&u=="FAX"?"fax":"";
  line(prefix+key+(type.empty()?"":";TYPE="+type)+":"+value);line(prefix+"X-ABLabel:"+escapeText(label));};
 for(const auto& v:c.phones){if(v.value.size()>96||v.label.size()>48)return false;property("TEL;VALUE=text",v.label,escapeText(v.value));}
 for(const auto& v:c.emails){if(v.value.size()>254||v.label.size()>48)return false;property("EMAIL",v.label,escapeText(v.value));}
 for(const auto& a:c.addresses){if(a.label.size()>48||a.street.size()>192||a.houseNumber.size()>32||a.postalCode.size()>32||a.city.size()>128||a.region.size()>96||a.country.size()>96)return false;
  property("ADR",a.label,";;"+escapeText(a.street+(a.houseNumber.empty()?"":" "+a.houseNumber))+";"+escapeText(a.city)+";"+escapeText(a.region)+";"+escapeText(a.postalCode)+";"+escapeText(a.country));}
 line("END:VCARD");if(result.size()>16384)return false;output=std::move(result);return true;
}
std::string plainNotes(const std::string& input,bool html){
 if(!html)return input;std::string out;bool tag=false,quoted=false;char quote=0;
 for(size_t i=0;i<input.size();++i){char c=input[i];if(c=='<'&&!tag){tag=true;quoted=false;continue;}
 if(tag){if((c=='"'||c=='\'')&&(!quoted||c==quote)){quoted=!quoted;quote=c;}if(c=='>'&&!quoted){tag=false;if(!out.empty()&&out.back()!='\n')out+='\n';}continue;}
 if(c=='&'){auto end=input.find(';',i+1);if(end!=std::string::npos&&end-i<=10){auto entity=input.substr(i+1,end-i-1);
 if(entity=="amp")c='&';else if(entity=="lt")c='<';else if(entity=="gt")c='>';else if(entity=="quot")c='"';else if(entity=="apos"||entity=="#39")c='\'';else if(entity=="nbsp")c=' ';else {out+=input.substr(i,end-i+1);i=end;continue;}i=end;}}
 out+=c;
 }return out;
}
}

#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cerrno>
namespace hp {
inline constexpr const char* energyCsvHeader="timestamp,average_w,minimum_w,maximum_w,import_kwh,export_kwh,resolution_seconds";
struct EnergyCsvRecord {uint32_t timestamp=0,resolution=0;double values[5]={NAN,NAN,NAN,NAN,NAN};};
inline bool parseEnergyCsv(const char* line,EnergyCsvRecord& r){
 if(!line||strlen(line)>240)return false;
 char copy[241];strcpy(copy,line);char* columns[7];char* p=copy;
 for(unsigned i=0;i<7;++i){columns[i]=p;char* comma=strchr(p,',');if(i<6){if(!comma)return false;*comma=0;p=comma+1;}else if(comma)return false;}
 auto integer=[](const char* text,uint32_t& value){if(!*text)return false;for(auto* p=text;*p;++p)if(*p<'0'||*p>'9')return false;
  errno=0;char* end;const auto n=strtoull(text,&end,10);if(errno||*end||n>UINT32_MAX)return false;value=n;return true;};
 if(!integer(columns[0],r.timestamp)||r.timestamp<1577836800||!integer(columns[6],r.resolution)||r.resolution<5||r.resolution>86400)return false;
 for(unsigned i=0;i<5;++i){const char* s=columns[i+1];if(!*s||!strcmp(s,"null")){r.values[i]=NAN;continue;}
  errno=0;char* end;r.values[i]=strtod(s,&end);if(errno||*end||!std::isfinite(r.values[i]))return false;
 }return true;
}
inline EnergyCsvRecord mergeEnergyCsv(const EnergyCsvRecord& old,const EnergyCsvRecord& incoming){
 // Resolution is part of the measurement, not just a rendering preference.
 if(incoming.resolution>old.resolution)return old;
 auto result=incoming.resolution<old.resolution?incoming:old;
 const auto& other=incoming.resolution<old.resolution?old:incoming;
 for(unsigned i=0;i<5;++i)if(!std::isfinite(result.values[i])&&std::isfinite(other.values[i]))result.values[i]=other.values[i];
 return result;
}
inline int formatEnergyCsv(const EnergyCsvRecord& r,char* output,size_t capacity){
 char values[5][32];for(unsigned i=0;i<5;++i){if(std::isfinite(r.values[i]))snprintf(values[i],sizeof(values[i]),"%.10g",r.values[i]);else strcpy(values[i],"null");}
 return snprintf(output,capacity,"%lu,%s,%s,%s,%s,%s,%lu\n",static_cast<unsigned long>(r.timestamp),values[0],values[1],values[2],values[3],values[4],static_cast<unsigned long>(r.resolution));
}
}

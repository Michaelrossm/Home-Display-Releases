#pragma once
#include <string>
#include <vector>
namespace hp {
struct DavCollection {std::string href,name;};
struct DavResource {std::string href,data;};
struct DavDiscovery {std::string principal;std::vector<std::string> homes;std::vector<DavCollection> calendars;std::vector<DavResource> cards,resources;bool truncated=false;};
namespace CalDavDiscovery {
bool parse(const char* xml,size_t length,DavDiscovery& result,bool contacts=false);
bool httpsUrl(const std::string& url);
std::string origin(const std::string& url);
std::string resolve(const std::string& base,const std::string& href);
bool allowedTarget(const std::string& root,const std::string& target,bool icloud);
bool appleAppPassword(const char* password);
}
}

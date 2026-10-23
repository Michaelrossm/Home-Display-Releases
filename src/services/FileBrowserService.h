#pragma once
#include <Arduino.h>
#include <FS.h>
#include <atomic>
#include <memory>
#include <vector>
#include "ImageCodec.h"
#include "NasService.h"
#include "../core/DirectoryPageCache.h"
namespace hp {
struct FileBrowserEntry {String name,uri;uint64_t size=0;bool directory=false;};
struct DirectoryRequest {std::atomic<bool> canceled{false},ready{false};std::vector<FileBrowserEntry> entries;String message;bool more=false;};
struct ImageSource {uint8_t* bytes=nullptr;size_t size=0;String uri;DirectoryPageCache::Revision revision;~ImageSource();};
struct ImageRequest {std::atomic<bool> canceled{false},ready{false};std::shared_ptr<DecodedImage> image;String message;bool savedWallpaper=false;
    std::shared_ptr<ImageSource> source;uint32_t queuedAt=0,queueMs=0,readMs=0,decodeMs=0,sourceWidth=0,sourceHeight=0;};
class FileBrowserService {
public:
    static String normalizeUri(const String& uri);
    // Worker-only provider access shared by native Explorer and Lua jobs.
    static bool visit(const String& uri,NasService::Visitor visitor,void* context,const std::atomic<bool>* cancel=nullptr);
    static bool stat(const String& uri,NasService::FileInfo& info);
    static bool read(const String& uri,uint8_t* bytes,size_t capacity,size_t& size,const std::atomic<bool>* cancel=nullptr);
    static bool readRange(const String& uri,uint64_t offset,uint8_t* bytes,size_t capacity,size_t& size,uint64_t& total,const std::atomic<bool>* cancel=nullptr);
    static std::shared_ptr<ImageRequest> imageNow(const String& uri,uint32_t width,uint32_t height,bool wallpaper=false,bool cover=false,const std::atomic<bool>* cancel=nullptr,bool center=false,bool rgb565=false);
    static bool saveNativeImage(const String& path,const DecodedImage& image);
    static std::shared_ptr<DirectoryRequest> list(const String& uri,size_t offset=0);
    // Existing workers only. Full keys retain sort/filter/offset semantics.
    static bool cachedPage(const String& uri,const String& key,std::vector<NasService::FileInfo>& items,bool& more);
    static DirectoryPageCache::Revision directoryRevision();
    static void cachePage(const String& uri,const String& key,const std::vector<NasService::FileInfo>& items,bool more,DirectoryPageCache::Revision expected);
    static std::shared_ptr<ImageRequest> image(const String& uri,uint32_t width,uint32_t height,bool wallpaper=false,bool rgb565=false,std::shared_ptr<ImageSource> source={});
    static bool validUri(const String& uri);
    static bool removePhoto(const String&,DirectoryPageCache::Revision,const std::atomic<bool>*,String& error); // Worker-only, SD/USB /Bilder only.
    static File openRead(const String& uri); // Existing generation-bound SD/USB providers; worker only.
    static String parent(const String& uri);
    static uint32_t wallpaperRevision(){return wallpaperRevision_.load();}
    static void notifyWallpaperChanged(){++wallpaperRevision_;}
private:
    static std::atomic<uint32_t> wallpaperRevision_;
};
}

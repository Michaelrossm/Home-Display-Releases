#pragma once
#include "FileBrowserService.h"
#include "../apps/photos/PhotoFramePolicy.h"
#include <array>
namespace hp {
struct PhotoSettings {
    static constexpr size_t maxFolders=8,maxFavorites=32;
    std::array<String,maxFolders> folders;std::array<String,maxFavorites> favorites;
    uint16_t start=420,end=1140,interval=30,returnAfter=30,renderEdge=1280;uint8_t days=127;
    bool schedule=false,recursive=false,automaticFolders=true,takeover=true,sortDate=true,descending=true;
};
// Logical album paths deliberately omit the medium: SD/USB Bilder/Urlaub is
// one album. Files retain their full source URI and are never moved/copied.
inline String photoAlbum(const String& uri){const int separator=uri.indexOf(":/"),slash=uri.lastIndexOf('/');
    return separator>=0&&slash>separator+1?uri.substring(separator+2,slash):String("Ohne Ordner");}
inline bool photoInAlbum(const String& uri,const String& album){const String path=photoAlbum(uri);
    return !album.length()||path==album||path.startsWith(album+"/");}
struct PhotoLibrary {
    static constexpr size_t limit=1024,pathSize=240;
    char paths[limit][pathSize]{};uint64_t modified[limit]{};uint16_t order[limit]{},count=0;bool truncated=false;
    const char* path(size_t index)const{return paths[order[index]];}
};
struct PhotoScanRequest {std::atomic<bool> canceled{false},ready{false};std::shared_ptr<PhotoLibrary> library;String message;};
struct PhotoSettingsRequest {std::atomic<bool> ready{false};PhotoSettings settings;uint32_t expectedRevision=0;bool success=false,load=false;String message;};
struct PhotoDeleteRequest {std::atomic<bool> ready{false},canceled{false};String uri,message;DirectoryPageCache::Revision revision;bool success=false;};
class PhotoLibraryService {
public:
    static PhotoLibraryService& instance();
    bool begin(bool force=false);
    void pollUi(bool interrupted=false); // Existing UI lock; never does storage/network I/O.
    const PhotoSettings& settings()const{return settings_;}
    bool loaded()const{return loaded_;}
    bool saving()const{return bool(pending_);}
    const String& status()const{return status_;}
    uint32_t revision()const{return revision_;}
    void notifyMediaChanged(){mediaDirty_.store(true);}
    bool save(const PhotoSettings& settings);
    // Worker-only persistence API shared with the authenticated web frontend.
    static bool readStored(PhotoSettings&,String& error);
    static bool writeStored(const PhotoSettings&,String& error);
    static String settingsJson(const PhotoSettings&);
    static bool settingsFromJson(const String&,PhotoSettings&,String& error);
    void requestControl(uint8_t action){if(action>=1&&action<=3)control_.store(action);} // 1 pause, 2 resume schedule, 3 manual start; UI executes.
    bool toggleFavorite(const String& uri);
    bool favorite(const String& uri)const;
    static bool validPhotoUri(const String& uri);
    static bool validSettings(const PhotoSettings& settings);
    static std::shared_ptr<PhotoDeleteRequest> removePhoto(const String&,DirectoryPageCache::Revision);
    static std::shared_ptr<PhotoScanRequest> scan(const PhotoSettings&,bool favorites=false);
    bool startFrame(bool automatic=false);
    void stopFrame();
    bool frameActive()const{return frame_;}
    void frameFailed();
private:
    PhotoSettings settings_;std::shared_ptr<PhotoSettingsRequest> pending_;
    String status_;uint32_t revision_=0,lastPoll_=0,retryAt_=0,configRevision_=0;bool loaded_=false,frame_=false,automatic_=false;
    bool previousWindow_=false,pausedWindow_=false;int windowKey_=0;std::atomic<uint8_t> control_{0};
    std::atomic<bool> mediaDirty_{false};
};
}

#pragma once
#include "MusicTypes.h"

namespace hp {

class MusicLibrary {
public:
    static constexpr size_t kMaxTracks = 256;

    bool scan();
    bool begin();
    ~MusicLibrary();
    MusicLibrary()=default;
    MusicLibrary(const MusicLibrary&)=delete;
    MusicLibrary& operator=(const MusicLibrary&)=delete;
    size_t count() const { return count_; }
    const MusicTrack* at(size_t i) const { return tracks_ && i < count_ ? &tracks_[i] : nullptr; }

private:
    bool supported(const char* name) const;
    void addFile(const char* path, const char* name, uint32_t size,uint64_t modified=0);

    MusicTrack* tracks_ = nullptr;
    size_t count_ = 0;
};

}

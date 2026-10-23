#pragma once
namespace hp {
// Existing package/user-data roots plus shared G-code exchange directory.
// Parents precede children. The callback must preserve existing files/directories.
template<class Ensure> bool ensureExternalStorageLayout(Ensure ensure){
    static constexpr const char* paths[]={"/Apps","/Apps/32","/GCode","/Firmware","/Bilder","/Musik","/Sprachen","/homepanel",
                         "/homepanel/data","/homepanel/media","/homepanel/logs","/homepanel/voice"};
    for(const char* path:paths){
        if(!ensure(path))return false;
    }
    return true;
}
}

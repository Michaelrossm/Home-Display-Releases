#pragma once
#include <Arduino.h>
#include <Stream.h>
#include "UpdateVerifier.h"
namespace hp { class UpdateService{public:static UpdateService&instance();bool begin();bool verifyPackageManifest(const String&manifest,const uint8_t*sig,size_t sigLen,UpdateManifest&out,String&error);bool preflight(const UpdateManifest&,String&error);bool writeFirmware(Stream&,const UpdateManifest&,String&error);bool backupAssets(const UpdateManifest&,String&error);bool writeAsset(Stream&,const char*label,const UpdateImageSpec&,String&error);bool activateFirmware(String&error);bool recoverAssetsFromBackup(String&error);private:bool copyPartitionToSd(const char*label,const char*file,uint32_t size,String&error);bool restorePartitionFromSd(const char*label,const char*file,String&error);bool streamPartition(Stream&,const char*label,const UpdateImageSpec&,String&error);UpdateManifest staged_{};bool verified_=false;}; }

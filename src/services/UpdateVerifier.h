#pragma once
#include <Arduino.h>
#include <Stream.h>
namespace hp {
struct UpdateImageSpec{String name;uint32_t size=0;String sha256;};
struct UpdateManifest{uint32_t schema=0;String product;String version;uint32_t versionCode=0;UpdateImageSpec firmware,systemData,appFs;};
class UpdateVerifier{public:static UpdateVerifier&instance();bool parseManifest(const String&json,UpdateManifest&out,String&error)const;bool verifyManifestSignature(const String&json,const uint8_t*sig,size_t sigLen,String&error)const;bool hashPartitionRaw(const char*label,uint32_t size,uint8_t out[32])const;bool verifyPartitionRaw(const char*label,uint32_t size,const String&hex,String&error)const;static uint32_t versionCode(const String&semver);static bool validSha256(const String&hex);private:static bool hexToBytes(const String&,uint8_t out[32]);};
}

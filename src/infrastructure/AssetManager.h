#pragma once
#include <Arduino.h>
namespace hp { enum class AssetStatus:uint8_t{Unknown,Ready,Missing,Invalid}; class AssetManager{public:static AssetManager&instance();bool begin();bool systemDataAvailable()const{return systemData_==AssetStatus::Ready;}bool appFsAvailable()const{return appFs_==AssetStatus::Ready;}AssetStatus systemDataStatus()const{return systemData_;}AssetStatus appFsStatus()const{return appFs_;}bool verifyAll();private:AssetStatus checkPartition(const char*label);AssetStatus systemData_=AssetStatus::Unknown,appFs_=AssetStatus::Unknown;}; }

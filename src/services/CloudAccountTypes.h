#pragma once
#include <Arduino.h>
#include <vector>
namespace hp {
enum class AccountProvider:uint8_t {Microsoft,Google,ICloud,CalDav};
enum class AccountCapability:uint8_t {Profile,Calendar,Mail,Contacts,Files};
using AccountCapabilities=uint8_t;
constexpr AccountCapabilities capabilityBit(AccountCapability c){return uint8_t(c)<5?uint8_t(1u<<uint8_t(c)):0;}
enum class CloudAccountState:uint8_t {Disconnected,Connecting,AwaitingUser,Connected,NeedsLogin,TokenExpired,InvalidCredentials,PermissionMissing,ServerUnavailable,TlsError,Error};
inline const char* cloudStateName(CloudAccountState s){const char* names[]={"Disconnected","Connecting","AwaitingUser","Connected","NeedsLogin","TokenExpired","InvalidCredentials","PermissionMissing","ServerUnavailable","TlsError","Error"};return uint8_t(s)<11?names[uint8_t(s)]:"Error";}
struct AccountInfo {
 String id,displayName,email,message;
 AccountProvider provider=AccountProvider::Google;
 CloudAccountState state=CloudAccountState::Disconnected;
 AccountCapabilities capabilities=0,supportedCapabilities=0;
 bool connected=false,enabled=true,phonebookEnabled=false;
};
struct CloudCalendarCollection {String id,displayName;bool readOnly=true;};
class CloudCalendarClient {
public:
 explicit CloudCalendarClient(const String& id):id_(id){}
 std::vector<CloudCalendarCollection> collections()const;
 bool requestRefresh()const;
 size_t copyEvents(struct EventRecord* output,size_t capacity)const;
 bool copyEventDetails(uint64_t localId,struct EventDetails& output)const;
private:String id_;
};
}

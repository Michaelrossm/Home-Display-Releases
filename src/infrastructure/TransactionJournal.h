#pragma once
#include <stdint.h>
namespace hp {
enum class UpdateState:uint8_t{Idle,PackageVerified,FirmwareWritten,BackupReady,AssetsWriting,AssetsVerified,BootPending,HealthChecking,Committed,RecoveryRequired};
struct __attribute__((packed)) UpdateJournalRecord{uint32_t magic;uint16_t formatVersion;uint16_t sequence;UpdateState state;uint8_t reserved[3];uint32_t firmwareVersion;uint32_t systemDataVersion;uint32_t appFsVersion;uint32_t crc;};
class TransactionJournal{
public: static TransactionJournal& instance(); bool begin(); bool latest(UpdateJournalRecord&out)const; bool append(UpdateState state,uint32_t fw=0,uint32_t sys=0,uint32_t app=0); bool clear(); UpdateState state()const{return current_.state;} uint16_t sequence()const{return current_.sequence;}
private: bool readSlot(int,UpdateJournalRecord&)const;bool valid(const UpdateJournalRecord&)const;bool writeSlot(int,const UpdateJournalRecord&);UpdateJournalRecord current_{};bool have_=false;
};
}

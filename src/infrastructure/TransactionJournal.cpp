#include "TransactionJournal.h"
#include "Crc32.h"
#include <Preferences.h>
#include <stddef.h>
namespace hp {
static constexpr uint32_t kMagic=0x48444A31; // HDJ1
static constexpr uint16_t kFormat=1;
static constexpr const char*kKey[2]={"record_a","record_b"};
static Preferences journalPrefs;
static bool prefsReady=false;
TransactionJournal& TransactionJournal::instance(){static TransactionJournal j;return j;}
bool TransactionJournal::valid(const UpdateJournalRecord&r)const{if(r.magic!=kMagic||r.formatVersion!=kFormat||r.sequence==0||r.state>UpdateState::RecoveryRequired)return false;return crc32(&r,offsetof(UpdateJournalRecord,crc))==r.crc;}
bool TransactionJournal::readSlot(int i,UpdateJournalRecord&r)const{if(!prefsReady)return false;return journalPrefs.getBytesLength(kKey[i])==sizeof(r)&&journalPrefs.getBytes(kKey[i],&r,sizeof(r))==sizeof(r)&&valid(r);}
bool TransactionJournal::begin(){if(!prefsReady)prefsReady=journalPrefs.begin("hd_journal",false);if(!prefsReady)return false;UpdateJournalRecord a{},b{};bool va=readSlot(0,a),vb=readSlot(1,b);if(!va&&!vb){current_={kMagic,kFormat,1,UpdateState::Idle,{0,0,0},0,0,0,0};current_.crc=crc32(&current_,offsetof(UpdateJournalRecord,crc));have_=writeSlot(1,current_);return have_;}current_=!va?b:!vb?a:((int16_t)(a.sequence-b.sequence)>=0?a:b);have_=true;return true;}
bool TransactionJournal::latest(UpdateJournalRecord&out)const{if(!have_)return false;out=current_;return true;}
bool TransactionJournal::writeSlot(int i,const UpdateJournalRecord&r){if(!prefsReady)return false;if(journalPrefs.putBytes(kKey[i],&r,sizeof(r))!=sizeof(r))return false;UpdateJournalRecord verify{};return journalPrefs.getBytes(kKey[i],&verify,sizeof(verify))==sizeof(verify)&&valid(verify)&&verify.sequence==r.sequence;}
bool TransactionJournal::append(UpdateState s,uint32_t fw,uint32_t sy,uint32_t ap){if(!have_&&!begin())return false;UpdateJournalRecord n=current_;n.sequence=(uint16_t)(n.sequence+1);if(!n.sequence)n.sequence=1;n.state=s;if(fw)n.firmwareVersion=fw;if(sy)n.systemDataVersion=sy;if(ap)n.appFsVersion=ap;n.crc=crc32(&n,offsetof(UpdateJournalRecord,crc));int slot=n.sequence&1u;if(!writeSlot(slot,n))return false;current_=n;return true;}
bool TransactionJournal::clear(){if(!have_&&!begin())return false;return append(UpdateState::Idle,0,0,0);}
}

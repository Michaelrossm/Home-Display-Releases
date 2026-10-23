#include "WebTlsIdentity.h"
#include "CredentialStore.h"
#include "StorageService.h"
#include "SettingsService.h"
#include <esp_random.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include <memory>
#include <mutex>
namespace hp {
namespace {
int randomBytes(void*,unsigned char* out,size_t n){esp_fill_random(out,n);return 0;}
struct Record {char certificate[2048];char key[512];~Record(){mbedtls_platform_zeroize(this,sizeof(*this));}};
}
size_t WebTlsIdentity::certificateSize()const{return strlen(certificate_)+1;}
size_t WebTlsIdentity::keySize()const{return strlen(key_)+1;}
bool WebTlsIdentity::load(){
 static std::mutex mutex;std::lock_guard<std::mutex> guard(mutex);
 if(loaded_)return true;
 // Arduino's boot/loop stack is only 8 KiB; keep PEM records off that stack.
 std::unique_ptr<Record> holder(new(std::nothrow) Record{});if(!holder)return false;
 Record& record=*holder;
 if(CredentialStore::instance().readOAuthRecord("web_tls",&record,sizeof(record))){
  if(!memchr(record.certificate,0,sizeof(record.certificate))||!memchr(record.key,0,sizeof(record.key)))return false;
 }else{
  // Never replace a damaged identity or an unpromoted atomic-write backup.
  for(const char* suffix:{".bin",".bin.bak",".bin.tmp"})
   if(StorageService::instance().exists(StorageArea::Config,String("/credentials/web_tls")+suffix))return false;
  mbedtls_pk_context key;mbedtls_pk_init(&key);
  mbedtls_x509write_cert cert;mbedtls_x509write_crt_init(&cert);
  unsigned char serialBytes[16];esp_fill_random(serialBytes,sizeof(serialBytes));serialBytes[0]&=0x7f;serialBytes[0]|=1;
  bool ok=mbedtls_pk_setup(&key,mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))==0&&
   mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,mbedtls_pk_ec(key),randomBytes,nullptr)==0;
  mbedtls_x509write_crt_set_md_alg(&cert,MBEDTLS_MD_SHA256);
  mbedtls_x509write_crt_set_subject_key(&cert,&key);mbedtls_x509write_crt_set_issuer_key(&cert,&key);
  // Stable mDNS name; verify fingerprint before trusting this self-signed certificate.
  const String hostname=SettingsService::instance().snapshot().hostname;
  bool validName=hostname.length()>0&&hostname.length()<=63;
  for(size_t i=0;i<hostname.length();++i){const char c=hostname[i];validName&=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-';}
  if(!validName||hostname[0]=='-'||hostname[hostname.length()-1]=='-'){
   mbedtls_x509write_crt_free(&cert);mbedtls_pk_free(&key);return false;
  }
  const String dns=hostname+".local",subject=String("CN=")+dns+",O=Home-Display";
  unsigned char san[73]{};san[0]=0x30;san[1]=dns.length()+2;san[2]=0x82;san[3]=dns.length();memcpy(san+4,dns.c_str(),dns.length());
  ok=ok&&mbedtls_x509write_crt_set_serial_raw(&cert,serialBytes,sizeof(serialBytes))==0&&
   validName&&mbedtls_x509write_crt_set_subject_name(&cert,subject.c_str()) ==0&&
   mbedtls_x509write_crt_set_issuer_name(&cert,subject.c_str()) ==0&&
   mbedtls_x509write_crt_set_validity(&cert,"20260101000000","20360101000000")==0&&
   mbedtls_x509write_crt_set_basic_constraints(&cert,0,-1)==0&&
   mbedtls_x509write_crt_set_key_usage(&cert,MBEDTLS_X509_KU_DIGITAL_SIGNATURE)==0&&
   mbedtls_x509write_crt_set_extension(&cert,"\x55\x1d\x11",3,0,san,dns.length()+4)==0&&
   mbedtls_pk_write_key_pem(&key,reinterpret_cast<unsigned char*>(record.key),sizeof(record.key))==0&&
   mbedtls_x509write_crt_pem(&cert,reinterpret_cast<unsigned char*>(record.certificate),sizeof(record.certificate),randomBytes,nullptr)==0;
  mbedtls_x509write_crt_free(&cert);mbedtls_pk_free(&key);
  if(!ok||!CredentialStore::instance().writeOAuthRecord("web_tls",&record,sizeof(record)))return false;
 }
 mbedtls_x509_crt parsed;mbedtls_x509_crt_init(&parsed);
 mbedtls_pk_context parsedKey;mbedtls_pk_init(&parsedKey);
 const bool valid=mbedtls_x509_crt_parse(&parsed,reinterpret_cast<const unsigned char*>(record.certificate),strlen(record.certificate)+1)==0&&
  mbedtls_pk_parse_key(&parsedKey,reinterpret_cast<const unsigned char*>(record.key),strlen(record.key)+1,nullptr,0,randomBytes,nullptr)==0&&
  mbedtls_pk_check_pair(&parsed.pk,&parsedKey,randomBytes,nullptr)==0;
 if(valid){unsigned char digest[32]{};char fingerprint[96]{};
  if(mbedtls_sha256(parsed.raw.p,parsed.raw.len,digest,0)==0){for(size_t i=0;i<32;++i)snprintf(fingerprint+i*3,4,i==31?"%02X":"%02X:",digest[i]);Serial.printf("[HTTPS] certificate SHA256=%s\n",fingerprint);}}
 mbedtls_pk_free(&parsedKey);mbedtls_x509_crt_free(&parsed);if(!valid)return false;
 memcpy(certificate_,record.certificate,sizeof(certificate_));memcpy(key_,record.key,sizeof(key_));loaded_=true;return true;
}
}

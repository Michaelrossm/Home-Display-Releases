#pragma once
#include <cstddef>
namespace hp {
// Device-specific key is persisted only through the encrypted credential store.
class WebTlsIdentity {
public:
 static WebTlsIdentity& instance(){static WebTlsIdentity identity;return identity;}
 bool load();
 const unsigned char* certificate() const {return reinterpret_cast<const unsigned char*>(certificate_);}
 const unsigned char* key() const {return reinterpret_cast<const unsigned char*>(key_);}
 size_t certificateSize() const;
 size_t keySize() const;
private:
 bool loaded_=false;
 char certificate_[2048]{};
 char key_[512]{};
};
}

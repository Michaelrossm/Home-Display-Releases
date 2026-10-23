#pragma once
namespace hp::update_key {
// Public verification key only. The private key must remain outside repositories.
inline constexpr const char kPem[] = R"HPKEY(-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEh92YclQZXoeojT5RyWMejSu3NHy5
8rSiuf7NsibpyNrtvD4pcM+czqG6v4cFH7fFpyPrGo6qYkjMXlHXHAIHuQ==
-----END PUBLIC KEY-----
)HPKEY";
}

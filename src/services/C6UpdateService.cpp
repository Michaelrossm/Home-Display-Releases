#include "C6UpdateService.h"
#include "NetworkService.h"
#include "NetworkWorker.h"
#include "../core/EventLog.h"
#include <esp32-hal-hosted.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <mbedtls/sha256.h>

namespace hp {
namespace {

// Home-Display C6 baseline.
// This intentionally matches tools/build_c6_slave.py and
// docs/C6_HOSTED_SLAVE_BUILD.md in this project.
constexpr char kExpectedC6Version[] = "2.12.13";

// Verified combined ESP-Hosted C6 image used by the Home-Display project.
constexpr char kC6ImageUrl[] =
    "https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.12.13.bin";

constexpr char kC6ImageSha256[] =
    "f8509949826c197a4fa3c7958943b4e8985cffdfdf4204c2847d8953905121c9";

constexpr int kC6ImageBytes = 1226976;

String versionString(uint32_t major, uint32_t minor, uint32_t patch) {
    return String(major) + "." + String(minor) + "." + String(patch);
}

bool shaMatches(const uint8_t digest[32]) {
    char actual[65]{};
    for (size_t i = 0; i < 32; ++i) {
        snprintf(actual + i * 2, 3, "%02x", digest[i]);
    }
    return strcmp(actual, kC6ImageSha256) == 0;
}

}

C6UpdateService& C6UpdateService::instance() {
    static C6UpdateService s;
    return s;
}

bool C6UpdateService::begin() {
    if (!lock_) lock_ = xSemaphoreCreateMutex();
    if (!lock_) return false;

    uint32_t hm=0,hn=0,hp=0,sm=0,sn=0,sp=0;
    hostedGetHostVersion(&hm,&hn,&hp);

    if (hostedIsInitialized()) {
        hostedGetSlaveVersion(&sm,&sn,&sp);
    }

    status_ =
        "Host " + versionString(hm,hn,hp) +
        " / C6 " + versionString(sm,sn,sp) +
        " / Ziel " + String(kExpectedC6Version);

    return true;
}

void C6UpdateService::setStatus(const String& text, bool busy) {
    if (!lock_) return;

    xSemaphoreTake(lock_, portMAX_DELAY);
    status_ = text;
    busy_ = busy;
    xSemaphoreGive(lock_);
}

void C6UpdateService::finish(const String& text) {
    NetworkService::instance().setC6Maintenance(false);
    setStatus(text, false);
}

bool C6UpdateService::inProgress() const {
    if (!lock_ || xSemaphoreTake(lock_, pdMS_TO_TICKS(20)) != pdTRUE) return true;

    const bool value = busy_;
    xSemaphoreGive(lock_);
    return value;
}

String C6UpdateService::status() const {
    if (!lock_ || xSemaphoreTake(lock_, pdMS_TO_TICKS(20)) != pdTRUE) {
        return "C6-Status nicht verfügbar.";
    }

    const String value = status_;
    xSemaphoreGive(lock_);
    return value;
}

bool C6UpdateService::requestOfficialUpdate() {
    if (!lock_ || !NetworkService::instance().online()) {
        setStatus(
            "Für das C6-Update wird eine Internetverbindung benötigt.",
            false
        );
        return false;
    }

    xSemaphoreTake(lock_, portMAX_DELAY);

    if (busy_) {
        xSemaphoreGive(lock_);
        return false;
    }

    busy_ = true;
    status_ = "Home-Display C6 2.12.13 wird vorbereitet ...";
    xSemaphoreGive(lock_);

    NetworkService::instance().setC6Maintenance(true);

    if (NetworkWorker::instance().submit(job, this, "c6-update")) {
        return true;
    }

    finish("Netzwerk-Worker ist ausgelastet.");
    return false;
}

void C6UpdateService::job(void* p) {
    static_cast<C6UpdateService*>(p)->run();
}

void C6UpdateService::run() {
    // The network subsystem owns the ESP-Hosted SDIO pin configuration.
    // Do not configure pins a second time from the C6 updater; doing so may
    // conflict with the running transport and depends on a newer BoardConfig.
    if (!hostedIsInitialized()) {
        finish(
            "ESP-Hosted/SDIO ist nicht initialisiert. "
            "C6 bei 0.0.0 gegebenenfalls einmal per UART flashen."
        );
        return;
    }

    uint32_t hm=0,hn=0,hp=0,sm=0,sn=0,sp=0;
    hostedGetHostVersion(&hm,&hn,&hp);
    hostedGetSlaveVersion(&sm,&sn,&sp);

    const String hostVersion = versionString(hm,hn,hp);
    const String slaveVersion = versionString(sm,sn,sp);

    if (slaveVersion == kExpectedC6Version) {
        finish(
            "C6 ist bereits auf Home-Display ESP-Hosted " +
            String(kExpectedC6Version) + "."
        );
        return;
    }

    setStatus(
        "Lade verifizierte Home-Display C6-Firmware " +
        String(kExpectedC6Version) + " ...",
        true
    );

    esp_http_client_config_t config{};
    config.url = kC6ImageUrl;
    config.timeout_ms = 20000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (!client) {
        finish("C6-Update: HTTPS-Client konnte nicht erstellt werden.");
        return;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        finish("HTTPS-Verbindung zum C6-Update fehlgeschlagen.");
        return;
    }

    const int contentLength = esp_http_client_fetch_headers(client);
    const int statusCode = esp_http_client_get_status_code(client);

    if (statusCode != 200 || contentLength != kC6ImageBytes) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        EventLog::instance().add(
            "ERROR",
            "C6_IMAGE_META",
            "HTTP=" + String(statusCode) +
            " bytes=" + String(contentLength)
        );

        finish(
            "C6-Image wurde abgelehnt: Serverantwort oder Dateigröße stimmt nicht."
        );
        return;
    }

    if (!hostedHasUpdate()) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        finish(
            "Die laufende C6-Firmware unterstützt kein SDIO-Update. "
            "Einmalig per UART die Home-Display-C6-2.12.13-Firmware flashen."
        );
        return;
    }

    if (!hostedBeginUpdate()) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        finish(
            "C6 hat das SDIO-Update abgelehnt. "
            "Einmalige UART-Recovery kann erforderlich sein."
        );
        return;
    }

    uint8_t buffer[2048];
    int remaining = contentLength;
    bool ok = true;

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);

    if (mbedtls_sha256_starts(&sha, 0) != 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        mbedtls_sha256_free(&sha);
        finish("SHA-256 für C6-Update konnte nicht gestartet werden.");
        return;
    }

    while (remaining > 0) {
        const int got = esp_http_client_read(
            client,
            reinterpret_cast<char*>(buffer),
            std::min<int>(remaining, sizeof(buffer))
        );

        if (got <= 0) {
            ok = false;
            break;
        }

        if (mbedtls_sha256_update(&sha, buffer, got) != 0) {
            ok = false;
            break;
        }

        if (!hostedWriteUpdate(buffer, got)) {
            ok = false;
            break;
        }

        remaining -= got;
        delay(0);
    }

    uint8_t digest[32]{};

    if (ok && mbedtls_sha256_finish(&sha, digest) != 0) {
        ok = false;
    }

    mbedtls_sha256_free(&sha);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!ok || remaining != 0) {
        finish(
            "C6-Update abgebrochen; Übertragung war unvollständig."
        );
        return;
    }

    if (!shaMatches(digest)) {
        EventLog::instance().add(
            "ERROR",
            "C6_IMAGE_HASH",
            "Downloaded C6 2.12.13 image SHA-256 mismatch"
        );

        finish(
            "C6-Update abgebrochen: SHA-256 der Firmware stimmt nicht."
        );
        return;
    }

    if (!hostedEndUpdate()) {
        finish(
            "C6-Update konnte nicht abgeschlossen werden; "
            "bestehende C6-Firmware bleibt aktiv."
        );
        return;
    }

    setStatus(
        "C6 " + slaveVersion +
        " -> Home-Display " + String(kExpectedC6Version) +
        " geschrieben. Aktiviere Update ...",
        true
    );

    EventLog::instance().add(
        "INFO",
        "C6_UPDATE",
        slaveVersion +
        " -> " + String(kExpectedC6Version) +
        " (Host " + hostVersion + ")"
    );

    if (!hostedActivateUpdate()) {
        finish(
            "C6-Firmware wurde übertragen, konnte aber nicht aktiviert werden."
        );
        return;
    }

    delay(800);
    ESP.restart();
}

}

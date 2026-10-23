#include "FileTextPreviewService.h"
#include "FileBrowserService.h"
#include "InternalFilesService.h"
#include "NetworkWorker.h"
#include "SecurityService.h"
#include <new>
#include <initializer_list>
#include <memory>

namespace hp {
struct FileTextPreviewService::Job {
    std::shared_ptr<FileTextPreviewRequest> result;
    String uri;
};

bool FileTextPreviewService::supports(const String& path) {
    const int dot = path.lastIndexOf('.');
    if (dot < 0) return false;
    String ext = path.substring(dot + 1); ext.toLowerCase();
    for (const char* allowed : {"txt", "md", "log", "json", "jsonl", "lua", "csv", "xml", "html", "htm",
                                "ini", "cfg", "conf", "yaml", "yml", "gcode", "gco", "gc", "py", "js", "css",
                                "sh", "bat", "toml", "properties", "svg", "nfo", "srt", "vtt"}) {
        if (ext == allowed) return true;
    }
    return false;
}

std::shared_ptr<FileTextPreviewRequest> FileTextPreviewService::request(const String& uri) {
    auto result = std::make_shared<FileTextPreviewRequest>();
    auto* job = new (std::nothrow) Job{result, uri};
    if (!job || !NetworkWorker::instance().submit(run, job, "file-text")) {
        delete job;
        result->message = "Dateidienst beschäftigt. Bitte erneut versuchen.";
        result->ready.store(true, std::memory_order_release);
    }
    return result;
}

void FileTextPreviewService::run(void* pointer) {
    std::unique_ptr<Job> job(static_cast<Job*>(pointer));
    auto req = job->result;
    auto finish = [&](const char* message) {
        if (req->canceled.load(std::memory_order_acquire)) return;
        req->message = message;
        req->ready.store(true, std::memory_order_release);
    };
    if (!supports(job->uri) || (!FileBrowserService::validUri(job->uri) && !InternalFilesService::isUri(job->uri)) || SecurityService::instance().tabletLocked()) {
        finish("Dieser Dateityp oder Pfad ist nicht für die Textvorschau freigegeben."); return;
    }
    // A bounded, single transfer also works for large NAS files. The first
    // 4 KiB are enough for a preview, without allocating the entire file.
    uint8_t bytes[4096]; size_t size = 0; uint64_t total = 0;
    const bool ok = InternalFilesService::isUri(job->uri) ?
        InternalFilesService::readRange(job->uri, bytes, sizeof(bytes), size, total, &req->canceled) :
        FileBrowserService::readRange(job->uri, 0, bytes, sizeof(bytes), size, total, &req->canceled);
    if (!ok) {
        finish("Lesen fehlgeschlagen. Datenträger und NAS-Verbindung prüfen."); return;
    }
    if (req->canceled.load() || SecurityService::instance().tabletLocked()) return;
    for (size_t i = 0; i < size; ++i) {
        if (bytes[i] == 0) { finish("Die Datei enthält Binärdaten. Keine Textvorschau."); return; }
    }
    String output; output.reserve(size + 128);
    for (size_t i = 0; i < size; ++i) {
        const uint8_t c = bytes[i];
        if (c == '\t' || c == '\r') output += ' ';
        else if (c == '\n' || c >= 32) output += char(c);
    }
    if (total > size) output += "\n\n[Es werden nur die ersten 4 KiB angezeigt.]";
    if (req->canceled.load()) return;
    req->text = std::move(output);
    finish("Schreibgeschützte Textvorschau geladen.");
}
}

#include "LvFsBridge.h"
#include "../services/StorageService.h"
#include <lvgl.h>


namespace hp {

static void* sdOpen(lv_fs_drv_t*, const char* path, lv_fs_mode_t mode) {
    if (mode != LV_FS_MODE_RD || !StorageService::instance().sdAvailable()) return nullptr;

    String p = path ? path : "";
    if (!p.startsWith("/")) p = "/" + p;

    File* file = new File(StorageService::instance().openSdRead(p));
    if (!file || !(*file)) {
        delete file;
        return nullptr;
    }
    return file;
}

static lv_fs_res_t sdClose(lv_fs_drv_t*, void* file_p) {
    if (!file_p) return LV_FS_RES_INV_PARAM;
    File* f = static_cast<File*>(file_p);
    f->close();
    delete f;
    return LV_FS_RES_OK;
}

static lv_fs_res_t sdRead(
    lv_fs_drv_t*,
    void* file_p,
    void* buf,
    uint32_t btr,
    uint32_t* br
) {
    if (!file_p || !buf || !br) return LV_FS_RES_INV_PARAM;
    File* f = static_cast<File*>(file_p);
    *br = f->read(static_cast<uint8_t*>(buf), btr);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sdSeek(
    lv_fs_drv_t*,
    void* file_p,
    uint32_t pos,
    lv_fs_whence_t whence
) {
    if (!file_p) return LV_FS_RES_INV_PARAM;

    File* f = static_cast<File*>(file_p);
    uint32_t target = pos;

    if (whence == LV_FS_SEEK_CUR) {
        target = f->position() + pos;
    } else if (whence == LV_FS_SEEK_END) {
        target = f->size() + pos;
    }

    return f->seek(target) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t sdTell(lv_fs_drv_t*, void* file_p, uint32_t* pos) {
    if (!file_p || !pos) return LV_FS_RES_INV_PARAM;
    *pos = static_cast<File*>(file_p)->position();
    return LV_FS_RES_OK;
}

static bool sdReady(lv_fs_drv_t*) {
    return StorageService::instance().sdAvailable();
}

LvFsBridge& LvFsBridge::instance() {
    static LvFsBridge b;
    return b;
}

bool LvFsBridge::begin() {
    if (registered_) return true;

    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = 'S';
    drv.cache_size = 1024;
    drv.ready_cb = sdReady;
    drv.open_cb = sdOpen;
    drv.close_cb = sdClose;
    drv.read_cb = sdRead;
    drv.seek_cb = sdSeek;
    drv.tell_cb = sdTell;

    lv_fs_drv_register(&drv);
    registered_ = true;
    return true;
}

}

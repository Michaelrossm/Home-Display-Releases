#include "ThemeController.h"
#include "ThemeCatalog.h"
#include "UiTheme.h"
#include "UiShell.h"
#include "../services/SettingsService.h"
#include "../services/ConfigService.h"
#include "../services/IdleService.h"

namespace hp {
ThemeController& ThemeController::instance(){static ThemeController c;return c;}

bool ThemeController::apply(uint8_t mode,uint8_t accent){
    auto& catalog=ThemeCatalog::instance();
    catalog.begin();
    if(!catalog.validMode(mode)||accent>5)return false;
    auto& config=ConfigService::instance();
    if(mode>=ThemeCatalog::kBuiltInCount) {
        const char* file=catalog.filename(mode);
        if(!file||!file[0]||!config.putString("ui_theme_file",file))return false;
    } else if(config.has("ui_theme_file")&&!config.remove("ui_theme_file")) return false;
    if(!SettingsService::instance().setTheme(mode,accent))return false;
    UiTheme::instance().setSelection(mode,accent);
    UiShell::instance().refreshTheme();
    IdleService::instance().activity();
    return true;
}
}

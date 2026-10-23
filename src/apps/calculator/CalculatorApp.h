#pragma once
#include "../../core/AppManager.h"
#include "CalculatorModel.h"
namespace hp {
class CalculatorApp final : public IApp {
public:
    const char* id() const override {return "calculator";}
    const char* name() const override {return "Rechner";}
    const char* icon() const override {return "@calculator";}
    const char* version() const override {return "1.0.0";}
    AppKind kind() const override {return AppKind::Native;}
    bool launcherVisible() const override {return true;}
    bool begin() override {return true;}
    void open(lv_obj_t* parent) override;
    void close() override;
private:
    CalculatorModel model_;
    lv_obj_t *root_=nullptr,*display_=nullptr,*history_=nullptr;
    void update();
};
}

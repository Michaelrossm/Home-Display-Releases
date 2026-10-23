#pragma once
#include <Arduino.h>
#include "../services/ConfigService.h"
namespace hp { enum class RecoveryAction:uint8_t{Restart,PreviousFirmware,RepairAssets,ResetNetwork,ResetUi,ResetWebApps,ResetCalendarAccounts,ResetMedia,ResetUserSettings,FullFactoryReset}; class RecoveryManager{public:static RecoveryManager&instance();bool required()const;String reason()const;bool execute(RecoveryAction,bool confirmed=false);}; }

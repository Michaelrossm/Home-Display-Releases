#pragma once
#include <stdint.h>
namespace hp { class ConfigService; class ConfigMigration { public: static bool run(ConfigService&,uint32_t from,uint32_t to); private: static bool v1ToV2(ConfigService&); }; }

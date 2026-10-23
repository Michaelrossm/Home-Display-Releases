#pragma once
#include <Arduino.h>
#include <cstddef>

namespace hp {

struct AppRegistryEntry;

class HomeAppOrderService {
public:
    static HomeAppOrderService& instance();

    // Stable-sort user-installable registry entries according to the persisted
    // home-screen order. Entries unknown to the stored list keep their normal
    // relative order and are appended after known entries.
    void sortEntries(AppRegistryEntry* entries, size_t count) const;

    // Move one app relative to another using the current registry order as the
    // source of truth. This is called outside the LVGL touch callback.
    bool move(const String& draggedId, const String& targetId, bool after);

private:
    HomeAppOrderService() = default;
    static constexpr const char* kConfigKey = "ui_app_order";

    static int tokenRank(const String& list, const String& id);
    static bool hasToken(const String& list, const String& id);
    static String removeToken(const String& list, const String& id);
    static String appendMissing(const String& base, const String& additions);
    static String insertRelative(const String& list,
                                 const String& id,
                                 const String& target,
                                 bool after);
};

} // namespace hp

#include "HomeAppOrderService.h"
#include "ConfigService.h"
#include "../core/AppRegistry.h"

namespace hp {

HomeAppOrderService& HomeAppOrderService::instance() {
    static HomeAppOrderService s;
    return s;
}

int HomeAppOrderService::tokenRank(
    const String& list, const String& id) {
    if (!id.length()) return -1;
    size_t start = 0;
    int rank = 0;
    while (start <= list.length()) {
        int end = list.indexOf('|', start);
        if (end < 0) end = list.length();
        if (list.substring(start, end) == id) return rank;
        if (static_cast<size_t>(end) >= list.length()) break;
        start = static_cast<size_t>(end) + 1;
        ++rank;
    }
    return -1;
}

bool HomeAppOrderService::hasToken(
    const String& list, const String& id) {
    return tokenRank(list, id) >= 0;
}

String HomeAppOrderService::removeToken(
    const String& list, const String& id) {
    String out;
    out.reserve(list.length());
    size_t start = 0;
    while (start <= list.length()) {
        int end = list.indexOf('|', start);
        if (end < 0) end = list.length();
        const String token = list.substring(start, end);
        if (token.length() && token != id) {
            if (out.length()) out += '|';
            out += token;
        }
        if (static_cast<size_t>(end) >= list.length()) break;
        start = static_cast<size_t>(end) + 1;
    }
    return out;
}

String HomeAppOrderService::appendMissing(
    const String& base, const String& additions) {
    String out = base;
    out.reserve(base.length() + additions.length() + 8);

    size_t start = 0;
    while (start <= additions.length()) {
        int end = additions.indexOf('|', start);
        if (end < 0) end = additions.length();
        const String token = additions.substring(start, end);
        if (token.length() && !hasToken(out, token)) {
            if (out.length()) out += '|';
            out += token;
        }
        if (static_cast<size_t>(end) >= additions.length()) break;
        start = static_cast<size_t>(end) + 1;
    }
    return out;
}

String HomeAppOrderService::insertRelative(
    const String& list,
    const String& id,
    const String& target,
    bool after) {
    String without = removeToken(list, id);
    String out;
    out.reserve(without.length() + id.length() + 2);

    bool inserted = false;
    size_t start = 0;
    while (start <= without.length()) {
        int end = without.indexOf('|', start);
        if (end < 0) end = without.length();
        const String token = without.substring(start, end);

        if (token.length()) {
            if (!after && token == target && !inserted) {
                if (out.length()) out += '|';
                out += id;
                inserted = true;
            }

            if (out.length()) out += '|';
            out += token;

            if (after && token == target && !inserted) {
                out += '|';
                out += id;
                inserted = true;
            }
        }

        if (static_cast<size_t>(end) >= without.length()) break;
        start = static_cast<size_t>(end) + 1;
    }

    if (!inserted) {
        if (out.length()) out += '|';
        out += id;
    }
    return out;
}

void HomeAppOrderService::sortEntries(
    AppRegistryEntry* entries, size_t count) const {
    if (!entries || count < 2) return;

    const String saved =
        ConfigService::instance().getString(kConfigKey, "");
    if (!saved.length()) return;

    // Sort launcher-visible slots independently of app-management rights.
    // This needs one temporary registry entry
    // instead of two additional arrays of 48 String-heavy entries.
    for (size_t i = 0; i < count; ++i) {
        if (!entries[i].launcherVisible) continue;

        size_t best = i;
        int bestRank = tokenRank(saved, entries[i].id);

        for (size_t j = i + 1; j < count; ++j) {
            if (!entries[j].launcherVisible) continue;

            const int candidateRank =
                tokenRank(saved, entries[j].id);

            const bool candidateBetter =
                candidateRank >= 0 &&
                (bestRank < 0 || candidateRank < bestRank);

            if (candidateBetter) {
                best = j;
                bestRank = candidateRank;
            }
        }

        if (best != i) {
            AppRegistryEntry tmp = entries[i];
            entries[i] = entries[best];
            entries[best] = tmp;
        }
    }
}

bool HomeAppOrderService::move(
    const String& draggedId,
    const String& targetId,
    bool after) {
    if (!draggedId.length() || !targetId.length() ||
        draggedId == targetId)
        return false;

    // Keep remembered IDs for temporarily disabled/uninstalled apps, then add
    // currently registered user apps. Both lists live in Arduino String heap
    // storage; no 48-entry registry/String arrays are placed on the task stack.
    String order =
        ConfigService::instance().getString(kConfigKey, "");
    order = appendMissing(
        order,
        AppRegistry::instance().launcherIdList());

    if (!hasToken(order, draggedId) ||
        !hasToken(order, targetId))
        return false;

    order = insertRelative(
        order, draggedId, targetId, after);

    return ConfigService::instance().putString(
        kConfigKey, order);
}

} // namespace hp

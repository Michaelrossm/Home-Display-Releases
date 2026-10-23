#include "CalendarController.h"
#include "CalendarApi.h"
#include "../lua/LuaJobService.h"
#include "../../services/CalendarSyncService.h"
#include "../../services/ConfigService.h"
#include "../../services/SecurityService.h"
#include <cstring>
#include <ctime>
#include <algorithm>

namespace hp {
    namespace {
        uint32_t id32(JsonVariantConst value) {
            const char *text = value.as<const char *>();
            return text ? strtoul(text, nullptr, 16) : 0;
        }
        uint64_t id64(JsonVariantConst value) {
            const char *text = value.as<const char *>();
            return text ? strtoull(text, nullptr, 16) : 0;
        }
        void execute(const LuaJobService::Request &request, LuaJobService::Result &result) {
            if (request.context != "native_view_load" && request.context != "native_view_save") {
                executeCalendarJob(request, result);
                return;
            }
            if (request.cancelled && request.cancelled->load()) {
                result.cancelled = true;
                return;
            }
            JsonDocument args, out;
            if (deserializeJson(args, request.body, DeserializationOption::NestingLimit(8))) {
                result.error = "calendar_json";
                return;
            }
            if (request.context == "native_view_load") {
                const auto saved = ConfigService::instance().getString("cal_native_v1", "{}");
                if (saved.length() > 512 || deserializeJson(out, saved))
                    out.clear();
                result.ok = true;
            } else if (request.context == "native_view_save") {
                SecurityWriteGuard guard;
                if (SecurityService::instance().tabletLocked()) {
                    result.error = "tablet_locked";
                    return;
                }
                if (request.body.length() > 512) {
                    result.error = "view_settings_limit";
                    return;
                }
                result.ok = ConfigService::instance().putString("cal_native_v1", request.body);
                if (!result.ok)
                    result.error = "view_settings_write_failed";
            }
            completeCalendarJob(out, result);
        }
    } // namespace
    void CalendarController::open(bool usersOnly) {
        close();
        usersOnly_=usersOnly;
        regionRefreshPending_ = true;
        active_ = true;
        selected = calendar::localDate(time(nullptr));
        if (!calendar::validDate(selected.year, selected.month, selected.day) ||
            selected.year < 1971)
            selected = {2026, 1, 1};
        bootstrap_ = usersOnly?2:1;
        queryPending_ = !usersOnly;
        dirty = true;
        message = "Lokale Kalender werden geladen";
    }
    void CalendarController::close() {
        active_ = false;
        LuaJobService::instance().cancelOwner(owner());
        job_ = 0;
        busy = false;
        operation_ = "";
        cursorId_ = "";
        cursorStart_ = 0;
        retryAt_ = 0;
        detailsReady = actionDone = postalReady = false;postal.clear();savedPersonId=0;
        truncated = false;
        events.clear();
        people.clear();
        calendars.clear();features.clear();
        details.clear();
        status.clear();
        region.clear();
        personLinks.clear();personNames.clear();contacts.clear();dateGerman=false;
    }
    bool CalendarController::submit(const char *op, JsonDocument &args) {
        if (!active_ || job_)
            return false;
        LuaJobService::Request request;
        request.owner = owner();
        request.kind = 5; // Native handler, not sandboxed storage.
        if(usersOnly_)request.lane=LuaJobService::Request::Lane::LocalTransfer;
        request.context = op;
        request.handler = execute;
        request.permissions =
            perm(AppPermission::CalendarRead) | perm(AppPermission::CalendarWrite);
        request.required = perm(AppPermission::CalendarRead);
        if (!strcmp(op, "query"))
            request.fileName = "calendar_query";
        if (!strcmp(op, "postal_lookup"))
            request.lane = LuaJobService::Request::Lane::Transfer;
        serializeJson(args, request.body);
        job_ = LuaJobService::instance().submit(request);
        if (!job_) {
            retryAt_ = millis() + 250;
            return false;
        }
        operation_ = op;
        busy = true;
        dirty = true;
        return true;
    }
    bool CalendarController::action(const char *op, JsonDocument &args) {
        if (job_)
            return false;
        actionDone = false;
        message = !strcmp(op,"postal_lookup") ? "Ortsdaten werden online geladen …" : "Wird gespeichert …";
        return submit(op, args);
    }
    bool CalendarController::loadDetails(const char *id) {
        JsonDocument args;
        args["id"] = id;
        detailsReady = false;
        return submit("details", args);
    }
    void CalendarController::select(calendar::Date date, calendar::ViewMode view) {
        if (!calendar::validDate(date.year, date.month, date.day) || date.year < 1971)
            return;
        const auto before = calendar::viewRange(selected, mode),
                   after = calendar::viewRange(date, view);
        if (before.from == after.from && before.to == after.to && mode == view) {
            selected = date;
            dirty = true;
            return;
        }
        selected = date;
        mode = view;
        refresh(false);
    }
    void CalendarController::refresh(bool invalidate) {
        if (job_ && operation_ == "query") {
            LuaJobService::instance().cancel(owner(), job_);
            job_ = 0;
            busy = false;
        }
        if (invalidate)
            for (auto &c : cache_)
                c.complete = false;
        const auto range = calendar::viewRange(selected, mode);
        for (const auto &c : cache_)
            if (c.complete && c.from == range.from && c.to == range.to && c.person == person &&
                c.calendarId == calendarId && c.preset == preset &&
                c.revision == CalendarService::instance().syncRevision() &&
                c.cloud == CalendarSyncService::instance().dataRevision()) {
                events = c.events;
                revision_ = c.revision;
                cloudRevision_ = c.cloud;
                truncated = false;
                queryPending_ = false;
                dirty = true;
                message = "Aus lokalem Cache";
                return;
            }
        queryPending_ = !usersOnly_;
        events.clear();
        cursorId_ = "";
        cursorStart_ = 0;
        truncated = false;
        dirty = true;
    }
    void CalendarController::query() {
        JsonDocument args;
        const auto range = calendar::viewRange(selected, mode);
        args["from"] = range.from;
        args["to"] = range.to;
        args["limit"] = 32;
        args["preset"] = preset;
        args["person_id"] = person;
        args["calendar_id"] = calendarId;
        if (cursorStart_) {
            args["cursor"]["id"] = cursorId_;
            args["cursor"]["start"] = cursorStart_;
        }
        if (submit("query", args)) {
            queryPending_ = false;
        }
    }
    void CalendarController::poll() {
        if (!active_)
            return;
        if (job_) {
            LuaJobService::Result result;
            if (!LuaJobService::instance().result(owner(), job_, result) || !result.done)
                return;
            if (!LuaJobService::instance().release(owner(), job_))
                return;
            job_ = 0;
            busy = false;
            if (result.ok && !result.cancelled) {
                JsonDocument doc;
                if (!deserializeJson(doc, result.body))
                    consume(doc);
                else
                    message = "Ungültige Kalenderantwort";
            } else {
                message = result.cancelled ? "Vorgang abgebrochen" : result.error;
                if(usersOnly_){
                    if(message=="settings_locked")message="System-PIN freigeben, um Benutzer zu bearbeiten.";
                    else if(message=="person_email_invalid")message="E-Mail-Adresse prüfen oder das Feld leer lassen.";
                    else if(message=="person_name_invalid")message="Vor-/Nachname prüfen oder kürzen (zusammen höchstens 39 UTF-8-Bytes).";
                    else if(message=="person_write_failed")message="Benutzer nicht gespeichert: PIN, freien Speicher und höchstens 16 Profile prüfen.";
                    else if(message=="profile_revision_conflict")message="Profil wurde geändert. Bitte neu auswählen; Entwurf vorher sichern.";
                    else if(message=="person_limit")message="Höchstens 16 Benutzerprofile möglich.";
                    else if(message=="contact_already_linked")message="Kontakt ist bereits mit einem anderen Benutzer verknüpft.";
                    else if(message=="profile_has_events")message="Löschen nicht möglich: Benutzer hat noch verknüpfte Termine.";
                    else if(message=="profile_has_accounts")message="Vor dem Löschen die Kalenderkonten lösen.";
                    else if(message=="profile_has_calendars"||message=="profile_has_links")message="Vor dem Löschen die verknüpften Kalender prüfen und lösen.";
                    else if(message=="profile_commit_pending"||message=="profile_storage_unavailable"||message=="profile_prepare_failed")message="Speicherung nicht abgeschlossen. Datenträger prüfen und erneut laden; Wiederherstellung bleibt vorgemerkt.";
                    else if(message=="person_preferences_corrupt"||message=="person_options_write_failed")message="Profilzuordnungen nicht gespeichert. Speicher prüfen und Ansicht aktualisieren.";
                }
                queryPending_ = false;
                retryAt_ = millis() + 5000;
            }
            dirty = true;
        }
        if (job_ || int32_t(millis() - retryAt_) < 0)
            return;
        JsonDocument args;
        if (bootstrap_) {
            if(bootstrap_==4&&(usersOnly_||!regionRefreshPending_))bootstrap_=5;
            static const char *ops[] = {"native_view_load", "calendars", "people", "region",
                                        "status"};
            if (submit(ops[bootstrap_ - 1], args)) {
                if (++bootstrap_ > 5)
                    bootstrap_ = 0;
            }
            return;
        }
        if (queryPending_) {
            query();
            return;
        }
        if (revision_ != CalendarService::instance().syncRevision() ||
            cloudRevision_ != CalendarSyncService::instance().dataRevision()) {
            refresh(false);
            bootstrap_ = 2;
        }
    }
    void CalendarController::consume(JsonDocument &doc) {
        if(operation_=="postal_lookup"){postal=doc;postalReady=true;message="Ortsdaten geladen";return;}
        if (operation_ == "native_view_load") {
            preset = std::min(2u, doc["preset"] | 0u);
            person = doc["person"] | 0u;
            calendarId = doc["calendar"] | 0u;
        } else if (operation_ == "calendars") {
            calendars.clear();features.clear();
            for (JsonObject row : doc["items"].as<JsonArray>()) {
                CalendarRecord c;
                c.id = id32(row["id"]);
                c.ownerPersonId = id32(row["owner"]);
                c.kind = row["kind"] | uint8_t(0);
                c.colorIndex = row["color"] | uint8_t(0);
                c.enabled = row["visible"] | false;
                c.provider = (row["editable"] | false) ? uint8_t(CalendarProvider::LocalHomePanel)
                                                       : uint8_t(CalendarProvider::GenericCalDav);
                strlcpy(c.name, row["name"] | "", sizeof(c.name));
                if(c.kind==uint8_t(CalendarKind::BirthdaysVirtual)||c.kind==uint8_t(CalendarKind::HolidaysVirtual))features.push_back(c);else calendars.push_back(c);
            }
            if(calendarId){bool ordinary=false;for(const auto& c:calendars)ordinary|=c.id==calendarId;if(!ordinary)calendarId=0;}
        } else if (operation_ == "people") {
            ++peopleGeneration;
            dateGerman=doc["date_format"]=="de";personLinks.clear();personNames.clear();
            people.clear();
            for (JsonObject row : doc["items"].as<JsonArray>()) {
                PersonRecord p;
                p.id = id32(row["id"]);
                p.revision = row["revision"]|uint32_t(1);
                p.colorIndex=row["color"]|uint8_t(0);
                if(people.size()<16)participantMasks[people.size()]=row["participant_mask"]|uint16_t(1u<<people.size());
                personLinks[row["id"].as<String>()]=row["contact_id"]|"";
                personNames[row["id"].as<String>()]["first"]=row["firstName"]|row["name"].as<const char*>();
                personNames[row["id"].as<String>()]["last"]=row["lastName"]|"";
                personNames[row["id"].as<String>()]["calendars"]=row["calendars"];
                personNames[row["id"].as<String>()]["contact_email"]=row["contact_email"];
                strlcpy(p.displayName, row["name"] | "", sizeof(p.displayName));
                strlcpy(p.email, row["email"] | "", sizeof(p.email));
                p.birthdayYear = row["birthday_year"] | uint16_t(0);
                p.birthdayMonth = row["birthday_month"] | uint8_t(0);
                p.birthdayDay = row["birthday_day"] | uint8_t(0);
                people.push_back(p);
            }
            if(person&&std::none_of(people.begin(),people.end(),[&](const auto& p){return p.id==person;}))person=0;
        } else if (operation_ == "user_profile") {
            profileDetails=doc;
        } else if (operation_ == "person_contacts") {
            auto rows=contacts["items"].is<JsonArray>()?contacts["items"].as<JsonArray>():contacts["items"].to<JsonArray>();
            for(auto item:doc["items"].as<JsonArray>())if(rows.size()<128)rows.add(item);
            contacts["more"]=doc["more"]|false;contacts["next_offset"]=doc["next_offset"]|0;
        } else if (operation_ == "person_preferences") {
            dateGerman=doc["date_format"]=="de";message="Datumsformat gespeichert";
        } else if (operation_ == "region") {
            region = doc;
            regionRefreshPending_ = false;
        }
        else if (operation_ == "status" || operation_ == "sync") {
            status = doc;
            ++statusGeneration;
            // The profile view never runs a month query (which normally stamps
            // these versions). Stamp its completed metadata bootstrap instead,
            // otherwise every idle poll would enqueue the same bootstrap again.
            if(usersOnly_){revision_=CalendarService::instance().syncRevision();cloudRevision_=CalendarSyncService::instance().dataRevision();}
        } else if (operation_ == "details") {
            details = doc;
            detailsReady = true;
        } else if (operation_ == "query") {
            const uint32_t revision = doc["revision"] | 0u, cloud = doc["cloud_revision"] | 0u;
            if (cursorStart_ && (revision != revision_ || cloud != cloudRevision_)) {
                refresh(true);
                return;
            }
            revision_ = revision;
            cloudRevision_ = cloud;
            for (JsonObject row : doc["items"].as<JsonArray>()) {
                if (events.size() >= maxVisibleEvents) {
                    truncated = true;
                    break;
                }
                Item item;
                item.event.localId = id64(row["id"]);
                item.event.calendarId = id32(row["calendar_id"]);
                item.event.revision = row["revision"] | 0u;
                item.event.startUtc = row["start"] | int64_t(0);
                item.event.endUtc = row["end"] | int64_t(0);
                item.occurrence = row["occurrence"] | item.event.startUtc;
                item.event.flags = ((row["all_day"] | false) ? EventAllDay : 0) |
                                   ((row["read_only"] | false) ? EventReadOnly : 0) |
                                   ((row["recurring"] | false) ? EventRecurring : 0);
                strlcpy(item.event.title, row["title"] | "", sizeof(item.event.title));
                strlcpy(item.event.location, row["location"] | "", sizeof(item.event.location));
                events.push_back(item);
            }
            const bool more = doc["more"] | false;
            if (more && !truncated) {
                cursorStart_ = doc["next_cursor"]["start"] | int64_t(0);
                cursorId_ = doc["next_cursor"]["id"] | "";
                queryPending_ = cursorStart_ && cursorId_.length();
            } else {
                auto &c = cache_[nextCache_++ % cache_.size()];
                const auto range = calendar::viewRange(selected, mode);
                c.from = range.from;
                c.to = range.to;
                c.person = person;
                c.calendarId = calendarId;
                c.preset = preset;
                c.revision = revision_;
                c.cloud = cloudRevision_;
                c.events = events;
                c.complete = !truncated;
            }
            message = truncated ? "Viele Termine: Tagesansicht für weitere Treffer öffnen"
                      : (doc["pending"] | false)
                          ? "Lokale Daten · Cloud wird im Hintergrund aktualisiert"
                          : "Kalender aktuell";
        } else {
            if(operation_=="person_save")savedPersonId=id32(doc["id"]);
            actionDone = true;
            message = "Gespeichert";
            refresh(true);
            if (operation_ == "visibility" || operation_ == "appearance" ||
                operation_ == "calendar_create" || operation_ == "person_save" || operation_ == "person_remove" ||
                operation_ == "collection_selection" || operation_ == "account_person")
                bootstrap_ = 2;
        }
    }
    uint32_t CalendarController::color(uint32_t id) const {
        static const uint32_t colors[] = {
            0x5FA8FF, 0xFF6969, 0x59CC96, 0xB68CFF, 0xF6AD55, 0x4DD5D8, 0xEF84B5, 0xD4C36A,
            0x80A6D6, 0xA3CC59, 0xD97AA5, 0x7699F0, 0xB19B81, 0xA7D8CD, 0xF49E76, 0xC3A7E0};
        for (const auto &c : calendars)
            if (c.id == id)
                return colors[c.colorIndex % 16];
        return colors[0];
    }
    bool CalendarController::editable(uint32_t id) const {
        for (const auto &c : calendars)
            if (c.id == id)
                return c.provider == uint8_t(CalendarProvider::LocalHomePanel) &&
                       c.kind <= uint8_t(CalendarKind::Household);
        return false;
    }
    uint32_t CalendarController::firstEditable() const {
        for (const auto &c : calendars)
            if (editable(c.id))
                return c.id;
        return 0;
    }
} // namespace hp

#include "CalendarApp.h"
#include "../../ui/CountryPicker.h"
#include "../../services/PersonName.h"
#include "../../services/ContactProtocol.h"
#include <esp_random.h>
#include "../../ui/InputKeyboard.h"
#include "CalendarController.h"
#include "BirthdayInput.h"
#include "../../ui/SettingsWidgets.h"
#include "../../services/SecurityService.h"
#include "../../services/CalendarSyncService.h"
#include "../../ui/SecurityUi.h"
#include "../phonebook/PhonebookFilter.h"
#include <ctime>
#include <cstdio>
#include <cstring>
#include <new>
#include <esp_heap_caps.h>

namespace hp {
    struct NativeCalendarPage {
        CalendarController controller;
        lv_obj_t *root = nullptr, *heading = nullptr, *status = nullptr, *board = nullptr,
                 *scroll = nullptr, *list = nullptr;
        lv_obj_t *person = nullptr, *calendar = nullptr, *preset = nullptr, *search = nullptr,
                 *keyboard = nullptr;
        lv_obj_t *editor = nullptr, *editorForm = nullptr, *editorTitle = nullptr, *editorCalendar = nullptr,
                 *start = nullptr, *end = nullptr, *allDay = nullptr;
        lv_obj_t *location = nullptr, *notes = nullptr, *repeat = nullptr, *interval = nullptr,
                 *count = nullptr, *until = nullptr, *reminder = nullptr;
        lv_obj_t *save = nullptr, *remove = nullptr, *removeOccurrence = nullptr,
                 *editorStatus = nullptr;
        lv_obj_t *address[7]{}, *participants[16]{};
        lv_obj_t *postalChoice=nullptr;
        bool postalPending=false;
        String postalCode,postalBefore[3],postalAuto[3];
        lv_obj_t *rows[12]{};
        unsigned rowItems[12]{}, listOffset = 0;
        uint32_t editCalendars[32]{};
        unsigned editCalendarCount = 0;
        JsonDocument draft;
        int64_t occurrence = 0;
        bool settings = false, editorLoaded = false, editorReadOnly = false, newEvent = false,
             deleteConfirm = false;
        bool usersOnly=false;
        lv_obj_t *userSearch=nullptr,*userList=nullptr;
        lv_obj_t *profileSections[3]{},*profileFields[9]{},*profileCreateCalendar=nullptr,*profileDelete=nullptr,*profileSave=nullptr,*profileCountryPicker=nullptr;
        lv_obj_t* profilePostalChoice=nullptr;
        uint32_t profileRequested=0,profileApplied=0;
        uint32_t profileRequestedGeneration=0,profileEditingId=0;
        uint32_t profileContactsGeneration=0;
        uint32_t profileRetryAt=0,profilePostalPerson=0;
        uint32_t profileEditingRevision=0;
        bool profileDeleteArmed=false;
        String profileContactRequest;
        String profileRequestToken;
        String usersSignature;
        lv_obj_t *profileColor=nullptr,*profileCalendars[32]{},*profileAccounts[5]{};
        uint32_t profileCalendarIds[32]{};
        String profileSignature,profileAccountsSignature;
        bool profileDirty=false,profilePopulating=false;
        lv_obj_t* profileConfirm=nullptr;
        unsigned pendingProfile=0;
        String pendingProfileDestination;
        bool unlocked = false;
        bool settingsBuilt = false;
        bool regionLoaded = false, contactsRequested=false, formattingBirthday=false, dateGermanShown=false, contactChanged=false;
        lv_obj_t *dateFormat=nullptr,*personContact=nullptr;
        lv_obj_t *visibility[32]{}, *sharedFeature[2]{}, *regionState = nullptr, *municipal[3]{};
        lv_obj_t *personName = nullptr, *personLastName = nullptr, *personEmail = nullptr, *birthday = nullptr,
                 *personChoice = nullptr;
        lv_obj_t *calendarName = nullptr, *calendarKind = nullptr, *calendarOwner = nullptr,
                 *calendarColor = nullptr;
        lv_obj_t *appearanceCalendar = nullptr, *appearanceName = nullptr, *accountStatus = nullptr;
        lv_obj_t *collectionAccount = nullptr, *accountPerson = nullptr, *collectionRows[16]{};
        uint8_t collectionSlots[5]{};
        unsigned collectionAccountCount = 0;
        uint32_t shownStatusGeneration = 0;
        bool collectionsDirty = true, filtersPending = false;
        lv_obj_t *side = nullptr, *listPrevious = nullptr, *listNext = nullptr;
        uint16_t theme = 0;
        unsigned markCount[42]{};
        uint32_t markColors[42][3]{};
        // Reuse the large month/overlap models instead of placing them on the
        // comparatively small BSP LVGL task stack on each paint or tap.
        calendar::MonthGridModel monthModel{};
        std::array<calendar::TimeBlock, 64> overlapScratch{};
        bool monthModelReady = false;
        calendar::Date drawnSelection{};
        int drawnYear = 0, drawnMonth = 0, drawnWidth = 0, drawnHeight = 0;
        int64_t drawnToday = 0, lastMinute = 0;
        lv_timer_t *timer = nullptr;
    };
    namespace {
        using namespace SettingsWidgets;
        constexpr int hourHeight = 44, timeHeader = 82;
        constexpr const char *reminderOptions =
            "Keine\nBei Beginn\n5 Minuten vorher\n15 Minuten vorher\n30 Minuten vorher\n1 Stunde "
            "vorher\n2 Stunden vorher\n1 Tag vorher\n2 Tage vorher\n1 Woche vorher";
        constexpr int reminderMinutes[] = {-1, 0, 5, 15, 30, 60, 120, 1440, 2880, 10080};
        const char *weekdays[] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
        const char *months[] = {"Januar",    "Februar", "März",     "April",
                                "Mai",       "Juni",    "Juli",     "August",
                                "September", "Oktober", "November", "Dezember"};
        void render(NativeCalendarPage &);
        void editor(NativeCalendarPage &, bool);
        void settings(NativeCalendarPage &);
        void renderUsersList(NativeCalendarPage &);
        void chooseProfile(NativeCalendarPage &, unsigned, const char* destination="");
        void enableProfile(NativeCalendarPage &,bool);
        void renderSettings(NativeCalendarPage &);
        void detachPageCallbacks(lv_obj_t *root, NativeCalendarPage *page) {
            lv_obj_remove_event_cb_with_user_data(root, nullptr, page);
            for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
                detachPageCallbacks(lv_obj_get_child(root, i), page);
        }
        bool same(calendar::Date a, calendar::Date b) {
            return a.year == b.year && a.month == b.month && a.day == b.day;
        }
        int64_t dayOf(calendar::Date d) { return calendar::dayNumber(d.year, d.month, d.day); }
        void idText(uint64_t id, char *out) {
            snprintf(out, 17, "%08lx%08lx", (unsigned long)(id >> 32), (unsigned long)id);
        }
        void timeText(int64_t epoch, char *out, size_t size) {
            time_t t = epoch;
            tm value{};
            localtime_r(&t, &value);
            strftime(out, size, "%Y-%m-%d %H:%M", &value);
        }
        int64_t parseTime(const char *text) {
            int y, m, d, h, min;
            char extra;
            if (!text || sscanf(text, "%d-%d-%d %d:%d%c", &y, &m, &d, &h, &min, &extra) != 5)
                return 0;
            return calendar::localEpoch({y, m, d}, h, min);
        }
        lv_obj_t *push(lv_obj_t *parent, const char *text, lv_event_cb_t callback, void *context,
                       int width = 100) {
            auto *b = lv_button_create(parent);
            UiTheme::instance().stylePrimaryButton(b);
            lv_obj_set_style_shadow_width(b, 0, 0);
            lv_obj_set_size(b, width, 40);
            auto *l = label(b, text, 0, &lv_font_montserrat_14);
            lv_obj_center(l);
            lv_obj_add_event_cb(b, callback, LV_EVENT_CLICKED, context);
            return b;
        }
        // LVGL's full keyboard is a relatively expensive widget. Keep the
        // initial calendar frame lean and allocate it only for text editing.
        void keyboardDismiss(NativeCalendarPage &p) {
            if (!p.keyboard || !lv_obj_is_valid(p.keyboard)) return;
            lv_keyboard_set_textarea(p.keyboard, nullptr);
            lv_obj_add_flag(p.keyboard, LV_OBJ_FLAG_HIDDEN);
            if(p.editorForm) lv_obj_set_style_pad_bottom(p.editorForm,0,0);
            if(p.editor) lv_obj_set_style_pad_bottom(p.editor,16,0);
        }
        void bubbleKeyboardTaps(lv_obj_t *object) {
            lv_obj_add_flag(object, LV_OBJ_FLAG_EVENT_BUBBLE);
            for (uint32_t i=0;i<lv_obj_get_child_count(object);++i)
                bubbleKeyboardTaps(lv_obj_get_child(object,i));
        }
        void outsideKeyboardTap(lv_event_t *event) {
            auto &p=*static_cast<NativeCalendarPage *>(lv_event_get_user_data(event));
            if (!p.keyboard || lv_obj_has_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN)) return;
            for(auto *target=lv_event_get_target_obj(event);target;target=lv_obj_get_parent(target))
                if(target==p.keyboard || lv_obj_has_class(target,&lv_textarea_class)) return;
            keyboardDismiss(p);
        }
        bool ensureKeyboard(NativeCalendarPage &p) {
            // Rebuilt editor/settings descendants must also bubble outside taps.
            if(p.root) for(uint32_t i=0;i<lv_obj_get_child_count(p.root);++i)
                bubbleKeyboardTaps(lv_obj_get_child(p.root,i));
            if (p.keyboard && lv_obj_is_valid(p.keyboard)) return true;
            if (!p.root) return false;
            p.keyboard = lv_keyboard_create(p.root);
            if (!p.keyboard) return false;
            lv_obj_set_size(p.keyboard, LV_PCT(100), 260);
            lv_obj_add_flag(p.keyboard, LV_OBJ_FLAG_FLOATING);
            lv_obj_align(p.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_add_flag(p.keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_event_cb(p.keyboard,
                [](lv_event_t *e) {
                    auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                    keyboardDismiss(p);
                }, LV_EVENT_READY, &p);
            lv_obj_add_event_cb(p.keyboard,
                [](lv_event_t *e) {
                    auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                    keyboardDismiss(p);
                }, LV_EVENT_CANCEL, &p);
            return true;
        }
        lv_obj_t *input(NativeCalendarPage &p, lv_obj_t *parent, const char *title, size_t length,
                        bool multiline = false) {
            label(parent, title, 0, &lv_font_montserrat_14);
            auto *obj = lv_textarea_create(parent);
            darkInput(obj);
            lv_textarea_set_max_length(obj, length);
            lv_textarea_set_one_line(obj, !multiline);
            lv_obj_set_width(obj, LV_PCT(100));
            if (multiline)
                lv_obj_set_height(obj, 110);
            lv_obj_add_event_cb(
                obj,
                [](lv_event_t *e) {
                    auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                    if (!ensureKeyboard(p)) return;
                    InputKeyboard::bind(p.keyboard,
                                             static_cast<lv_obj_t *>(lv_event_get_target(e)));
                    lv_obj_remove_flag(p.keyboard, LV_OBJ_FLAG_HIDDEN);
                    if(p.editor) lv_obj_set_style_pad_bottom(p.editor,276,0);
                    lv_obj_move_foreground(p.keyboard);
                    lv_obj_scroll_to_view(static_cast<lv_obj_t *>(lv_event_get_target(e)),
                                          LV_ANIM_OFF);
                },
                LV_EVENT_FOCUSED, &p);
            lv_obj_add_event_cb(obj, [](lv_event_t *e) {
                auto &p=*static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                if(!ensureKeyboard(p)) return;
                InputKeyboard::bind(p.keyboard,lv_event_get_target_obj(e));
                lv_obj_remove_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);
                if(p.editor) lv_obj_set_style_pad_bottom(p.editor,276,0);
                lv_obj_move_foreground(p.keyboard);
                lv_obj_scroll_to_view(lv_event_get_target_obj(e),LV_ANIM_OFF);
            }, LV_EVENT_CLICKED, &p);
            return obj;
        }
        void drawText(lv_layer_t *layer, const char *text, const lv_area_t &area, lv_color_t color,
                      const lv_font_t *font, bool center = false) {
            if (!layer || !text || !font || area.x1 > area.x2 || area.y1 > area.y2)
                return;
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.color = color;
            d.font = font;
            d.text = text;
            d.text_local = 1;
            d.align = center ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT;
            lv_draw_label(layer, &d, &area);
        }
        void rectangle(lv_layer_t *layer, const lv_area_t &area, lv_color_t color, int radius = 0,
                       int border = 0) {
            if (!layer || area.x1 > area.x2 || area.y1 > area.y2)
                return;
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_color = color;
            d.bg_opa = LV_OPA_COVER;
            d.radius = radius;
            d.border_width = border;
            d.border_color = UiTheme::instance().border();
            d.border_opa = LV_OPA_COVER;
            lv_draw_rect(layer, &d, &area);
        }
        unsigned marks(const NativeCalendarPage &p, calendar::Date date, uint32_t colors[3]) {
            const auto from = calendar::localEpoch(date),
                       to = calendar::localEpoch(calendar::dateFromDay(dayOf(date) + 1));
            unsigned total = 0;
            for (const auto &item : p.controller.events)
                if (item.event.endUtc > from && item.event.startUtc < to) {
                    if (total < 3)
                        colors[total] = p.controller.color(item.event.calendarId);
                    ++total;
                }
            return total;
        }
        size_t timeBlocks(const NativeCalendarPage &p, calendar::Date date,
                          std::array<calendar::TimeBlock, 64> &blocks) {
            const auto from = calendar::localEpoch(date),
                       to = calendar::localEpoch(calendar::dateFromDay(dayOf(date) + 1));
            size_t count = 0;
            for (unsigned i = 0; i < p.controller.events.size() && count < blocks.size(); ++i) {
                const auto &e = p.controller.events[i].event;
                if ((e.flags & EventAllDay) || e.endUtc <= from || e.startUtc >= to)
                    continue;
                auto minute = [&](int64_t epoch) {
                    time_t t = epoch;
                    tm value{};
                    localtime_r(&t, &value);
                    return value.tm_hour * 60 + value.tm_min;
                };
                const int start = e.startUtc <= from ? 0 : minute(e.startUtc),
                          end = e.endUtc >= to ? 1440 : minute(e.endUtc);
                blocks[count++] = {start, std::max(start + 15, end), i, 0, 1};
            }
            calendar::layoutOverlaps(blocks, count);
            return count;
        }
        void draw(lv_event_t *e) {
            auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
            auto &c = p.controller;
            auto &theme = UiTheme::instance();
            auto *layer = lv_event_get_layer(e);
            lv_area_t area;
            lv_obj_get_content_coords(p.board, &area);
            const int width = lv_area_get_width(&area), height = lv_area_get_height(&area);
            if (width <= 0 || height <= 32)
                return;
            const auto today = calendar::localDate(time(nullptr));
            char text[96];
            if (c.mode == calendar::ViewMode::Month) {
                if (!p.monthModelReady) return;
                const auto &model = p.monthModel;
                for (int column = 0; column < 7; ++column) {
                    lv_area_t a{area.x1 + column * width / 7, area.y1,
                                area.x1 + (column + 1) * width / 7 - 1, area.y1 + 25};
                    drawText(layer, weekdays[column], a, theme.textSecondary(), &hp_font_de_14,
                             true);
                }
                for (unsigned i = 0; i < 42; ++i) {
                    const auto &cell = model.cells[i];
                    const int column = i % 7, row = i / 7;
                    lv_area_t a{area.x1 + column * width / 7 + 3,
                                area.y1 + 32 + row * (height - 32) / 6 + 3,
                                area.x1 + (column + 1) * width / 7 - 4,
                                area.y1 + 32 + (row + 1) * (height - 32) / 6 - 4};
                    const bool selected = same(cell.date, c.selected),
                               current = same(cell.date, today);
                    rectangle(layer, a, selected ? theme.accent() : theme.backgroundAlt(), 12,
                              current ? 2 : 0);
                    snprintf(text, sizeof(text), "%d", cell.date.day);
                    lv_area_t number{a.x1, a.y1 + 8, a.x2, a.y1 + 37};
                    drawText(layer, text, number,
                             selected       ? theme.onAccent()
                             : cell.inMonth ? theme.textPrimary()
                                            : theme.textSecondary(),
                             &hp_font_de_24, true);
                    const auto *colors = p.markColors[i];
                    const auto total = p.markCount[i];
                    for (unsigned j = 0; j < std::min(3u, total); ++j) {
                        const int x = a.x1 + 10 + j * 14;
                        lv_area_t mark{x, a.y2 - 14, x + 8, a.y2 - 6};
                        rectangle(layer, mark, lv_color_hex(colors[j]), 4);
                    }
                    if (total > 3) {
                        snprintf(text, sizeof(text), "+%u", total - 3);
                        lv_area_t atext{a.x1 + 57, a.y2 - 23, a.x2, a.y2 - 3};
                        drawText(layer, text, atext,
                                 selected ? theme.onAccent() : theme.textSecondary(),
                                 &hp_font_de_14);
                    }
                }
                return;
            }
            const int days = c.mode == calendar::ViewMode::Week ? 7 : 1, left = 44,
                      columnWidth = (width - left) / days;
            const auto range = calendar::viewRange(c.selected, c.mode);
            const auto first = dayOf(range.first);
            for (int hour = 0; hour <= 24; ++hour) {
                const int y = area.y1 + timeHeader + hour * hourHeight;
                lv_area_t line{area.x1 + left, y, area.x2, y};
                rectangle(layer, line, theme.border());
                if (hour < 24) {
                    snprintf(text, sizeof(text), "%02d", hour);
                    lv_area_t a{area.x1, y - 8, area.x1 + 36, y + 14};
                    drawText(layer, text, a, theme.textSecondary(), &hp_font_de_14);
                }
            }
            for (int column = 0; column < days; ++column) {
                auto date = calendar::dateFromDay(first + column);
                const int x = area.x1 + left + column * columnWidth;
                snprintf(text, sizeof(text), "%s %d",
                         weekdays[calendar::weekday(date.year, date.month, date.day)], date.day);
                lv_area_t header{x, area.y1 + 3, x + columnWidth - 1, area.y1 + 27};
                drawText(layer, text, header,
                         same(date, today) ? theme.accent() : theme.textPrimary(), &hp_font_de_18,
                         true);
                lv_area_t vertical{x, area.y1 + 28, x, area.y2};
                rectangle(layer, vertical, theme.border());
                const auto from = calendar::localEpoch(date),
                           to = calendar::localEpoch(calendar::dateFromDay(first + column + 1));
                unsigned all = 0;
                for (const auto &item : c.events)
                    if ((item.event.flags & EventAllDay) && item.event.endUtc > from &&
                        item.event.startUtc < to) {
                        if (all < 2) {
                            lv_area_t block{x + 3, area.y1 + 29 + int(all) * 23,
                                            x + columnWidth - 4, area.y1 + 50 + int(all) * 23};
                            rectangle(layer, block, lv_color_hex(c.color(item.event.calendarId)),
                                      4);
                            block.x1 += 4;
                            drawText(layer, item.event.title, block, lv_color_hex(0x111820),
                                     &hp_font_de_14);
                        }
                        ++all;
                    }
                auto &blocks = p.overlapScratch;
                const auto count = timeBlocks(p, date, blocks);
                for (size_t i = 0; i < count; ++i) {
                    const auto &b = blocks[i];
                    const auto &item = c.events[b.source];
                    const int laneWidth = (columnWidth - 6) / int(b.lanes);
                    lv_area_t block{x + 3 + int(b.lane) * laneWidth,
                                    area.y1 + timeHeader + b.start * hourHeight / 60,
                                    x + 2 + int(b.lane + 1) * laneWidth,
                                    area.y1 + timeHeader + b.end * hourHeight / 60 - 2};
                    rectangle(layer, block, lv_color_hex(c.color(item.event.calendarId)), 5);
                    block.x1 += 4;
                    block.y1 += 2;
                    drawText(layer, item.event.title, block, lv_color_hex(0x111820),
                             &hp_font_de_14);
                }
                if (same(date, today)) {
                    time_t now = time(nullptr);
                    tm value{};
                    localtime_r(&now, &value);
                    const int y = area.y1 + timeHeader +
                                  (value.tm_hour * 60 + value.tm_min) * hourHeight / 60;
                    lv_area_t line{x, y, x + columnWidth - 1, y + 1};
                    rectangle(layer, line, theme.error());
                }
            }
        }
        void openItem(NativeCalendarPage &p, unsigned index) {
            if (index >= p.controller.events.size())
                return;
            const auto &event = p.controller.events[index].event;
            char id[17];
            idText(event.localId, id);
            bool virtualEvent = false;
            for (const auto &cal : p.controller.calendars)
                if (cal.id == event.calendarId)
                    virtualEvent = cal.kind == uint8_t(CalendarKind::BirthdaysVirtual) ||
                                   cal.kind == uint8_t(CalendarKind::HolidaysVirtual);
            if (virtualEvent) {
                auto &doc = p.controller.details;
                doc.clear();
                doc["id"] = id;
                char calendarId[9];
                snprintf(calendarId, sizeof(calendarId), "%08lx", (unsigned long)event.calendarId);
                doc["calendar_id"] = calendarId;
                doc["start"] = event.startUtc;
                doc["end"] = event.endUtc;
                doc["title"] = event.title;
                doc["read_only"] = true;
                doc["all_day"] = true;
                p.controller.detailsReady = true;
                p.newEvent = false;
                p.occurrence = event.startUtc;
                editor(p, false);
                return;
            }
            if (p.controller.loadDetails(id)) {
                p.occurrence = p.controller.events[index].occurrence;
                p.newEvent = false;
                p.editorLoaded = false;
                editor(p, false);
            }
        }
        void touched(lv_event_t *event) {
            auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(event));
            lv_point_t point;
            auto *indev = lv_indev_active();
            if (!indev)
                return;
            lv_indev_get_point(indev, &point);
            lv_area_t area;
            lv_obj_get_content_coords(p.board, &area);
            const int x = point.x - area.x1, y = point.y - area.y1,
                      width = lv_area_get_width(&area), height = lv_area_get_height(&area);
            if (p.controller.mode == calendar::ViewMode::Month) {
                const int index = calendar::monthCellAt(x, y, width, height);
                if (index < 0)
                    return;
                if (!p.monthModelReady) return;
                const auto &model = p.monthModel;
                p.listOffset = 0;
                p.controller.select(model.cells[index].date, p.controller.mode);
                render(p);
                return;
            }
            const int days = p.controller.mode == calendar::ViewMode::Week ? 7 : 1,
                      colWidth = (width - 44) / days;
            if (colWidth <= 0 || x < 44)
                return;
            const int column = std::min(days - 1, (x - 44) / colWidth);
            const auto range = calendar::viewRange(p.controller.selected, p.controller.mode);
            const auto date = calendar::dateFromDay(dayOf(range.first) + column);
            if (y >= 29 && y < 75) {
                const auto from = calendar::localEpoch(date),
                           to = calendar::localEpoch(calendar::dateFromDay(dayOf(date) + 1));
                unsigned row = 0;
                for (unsigned i = 0; i < p.controller.events.size(); ++i) {
                    const auto &item = p.controller.events[i].event;
                    if (!(item.flags & EventAllDay) || item.endUtc <= from || item.startUtc >= to)
                        continue;
                    if (int(row) == (y - 29) / 23 && row < 2) {
                        openItem(p, i);
                        return;
                    }
                    ++row;
                }
            }
            auto &blocks = p.overlapScratch;
            const auto count = timeBlocks(p, date, blocks);
            if (y >= timeHeader)
                for (size_t i = 0; i < count; ++i) {
                    const auto &b = blocks[i];
                    const int localX = (x - 44) % colWidth,
                              laneWidth = (colWidth - 6) / int(b.lanes);
                    if (y - timeHeader >= b.start * hourHeight / 60 &&
                        y - timeHeader < b.end * hourHeight / 60 &&
                        localX >= 3 + int(b.lane) * laneWidth &&
                        localX < 3 + int(b.lane + 1) * laneWidth) {
                        openItem(p, b.source);
                        return;
                    }
                }
            p.controller.selected = date;
            p.controller.dirty = true;
            render(p);
        }
        void renderList(NativeCalendarPage &p) {
            const char *search = lv_textarea_get_text(p.search);
            const auto from = calendar::localEpoch(p.controller.selected),
                       to = calendar::localEpoch(
                           calendar::dateFromDay(dayOf(p.controller.selected) + 1));
            unsigned match = 0, shown = 0;
            bool more = false;
            for (unsigned index = 0; index < p.controller.events.size(); ++index) {
                const auto &e = p.controller.events[index].event;
                if (p.controller.mode != calendar::ViewMode::Agenda &&
                    (e.endUtc <= from || e.startUtc >= to))
                    continue;
                String title = e.title, query = search;
                title.toLowerCase();
                query.toLowerCase();
                if (query.length() && title.indexOf(query) < 0)
                    continue;
                if (match++ < p.listOffset)
                    continue;
                if (shown >= 12) {
                    more = true;
                    break;
                }
                auto *row = p.rows[shown];
                p.rowItems[shown++] = index;
                char when[28], text[180];
                timeText(e.startUtc, when, sizeof(when));
                snprintf(text, sizeof(text), "%s\n%s", e.title,
                         (e.flags & EventAllDay) ? "Ganztägig" : when);
                lv_label_set_text(lv_obj_get_child(row, 0), text);
                lv_obj_set_style_border_color(row, lv_color_hex(p.controller.color(e.calendarId)),
                                              0);
                lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
            }
            for (unsigned i = shown; i < 12; ++i)
                lv_obj_add_flag(p.rows[i], LV_OBJ_FLAG_HIDDEN);
            if (p.listNext) {
                if (more)
                    lv_obj_remove_state(p.listNext, LV_STATE_DISABLED);
                else
                    lv_obj_add_state(p.listNext, LV_STATE_DISABLED);
            }
            if (p.listPrevious) {
                if (p.listOffset)
                    lv_obj_remove_state(p.listPrevious, LV_STATE_DISABLED);
                else
                    lv_obj_add_state(p.listPrevious, LV_STATE_DISABLED);
            }
        }
        void render(NativeCalendarPage &p) {
            auto &c = p.controller;
            const bool themeChanged = p.theme != UiTheme::instance().generation();
            p.theme = UiTheme::instance().generation();
            if (!calendar::validDate(c.selected.year, c.selected.month, c.selected.day)) {
                c.selected = calendar::localDate(time(nullptr));
                if (!calendar::validDate(c.selected.year, c.selected.month, c.selected.day))
                    c.selected = {2026, 1, 1};
                p.monthModelReady = false;
            }
            char text[120];
            snprintf(text, sizeof(text), "%s %d", months[c.selected.month - 1], c.selected.year);
            lv_label_set_text(p.heading, text);
            lv_label_set_text(p.status, c.message.c_str());
            if (p.settings) {
                renderSettings(p);
                return;
            }
            const bool month = c.mode == calendar::ViewMode::Month,
                       agenda = c.mode == calendar::ViewMode::Agenda;
            lv_obj_set_width(p.side, agenda ? LV_PCT(100) : 330);
            if (agenda)
                lv_obj_add_flag(p.scroll, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_remove_flag(p.scroll, LV_OBJ_FLAG_HIDDEN);
            if (month)
                lv_obj_set_height(p.board, LV_PCT(100));
            else
                lv_obj_set_height(p.board, timeHeader + 24 * hourHeight + 20);
            lv_obj_set_scroll_dir(p.scroll, month ? LV_DIR_NONE : LV_DIR_VER);
            if (month) {
                if (!p.monthModel.set(c.selected.year, c.selected.month, false)) {
                    lv_label_set_text(p.status, "Ungueltiges Kalenderdatum");
                    p.monthModelReady = false;
                    return;
                }
                p.monthModelReady = true;
                const auto &model = p.monthModel;
                lv_area_t area;
                lv_obj_get_content_coords(p.board, &area);
                const int width = lv_area_get_width(&area), height = lv_area_get_height(&area);
                const auto today = dayOf(calendar::localDate(time(nullptr)));
                const bool full = themeChanged || p.drawnYear != c.selected.year ||
                                  p.drawnMonth != c.selected.month || p.drawnWidth != width ||
                                  p.drawnHeight != height || p.drawnToday != today;
                for (unsigned i = 0; i < 42; ++i) {
                    uint32_t colors[3]{};
                    const auto count = c.events.empty() ? 0u : marks(p, model.cells[i].date, colors);
                    const bool selected = same(model.cells[i].date, c.selected),
                               prior = same(model.cells[i].date, p.drawnSelection);
                    const bool changed = count != p.markCount[i] ||
                                         memcmp(colors, p.markColors[i], sizeof(colors)) ||
                                         selected != prior;
                    p.markCount[i] = count;
                    memcpy(p.markColors[i], colors, sizeof(colors));
                    if (!full && changed && width > 0 && height > 32) {
                        const int column = i % 7, row = i / 7;
                        lv_area_t cell{area.x1 + column * width / 7,
                                       area.y1 + 32 + row * (height - 32) / 6,
                                       area.x1 + (column + 1) * width / 7 - 1,
                                       area.y1 + 32 + (row + 1) * (height - 32) / 6 - 1};
                        lv_obj_invalidate_area(p.board, &cell);
                    }
                }
                p.drawnYear = c.selected.year;
                p.drawnMonth = c.selected.month;
                p.drawnWidth = width;
                p.drawnHeight = height;
                p.drawnToday = today;
                p.drawnSelection = c.selected;
                if (full)
                    lv_obj_invalidate(p.board);
            } else {
                p.drawnYear = 0;
                p.monthModelReady = false;
                lv_obj_invalidate(p.board);
            }
            renderList(p);
            String people = "Gesamtkalender";
            for (const auto &person : c.people)
                people += "\n" + String(person.displayName);
            if (people != lv_dropdown_get_options(p.person))
                lv_dropdown_set_options(p.person, people.c_str());
            unsigned personIndex = 0;
            for (unsigned i = 0; i < c.people.size(); ++i)
                if (c.people[i].id == c.person)
                    personIndex = i + 1;
            lv_dropdown_set_selected(p.person, personIndex);
            String calendars = "Alle Kalender";
            for (const auto &cal : c.calendars)
                calendars += "\n" + String(cal.name);
            if (calendars != lv_dropdown_get_options(p.calendar))
                lv_dropdown_set_options(p.calendar, calendars.c_str());
            unsigned calIndex = 0;
            for (unsigned i = 0; i < c.calendars.size(); ++i)
                if (c.calendars[i].id == c.calendarId)
                    calIndex = i + 1;
            lv_dropdown_set_selected(p.calendar, calIndex);
            lv_dropdown_set_selected(p.preset, c.preset);
        }
        void dismissEditor(NativeCalendarPage &p) {
            p.postalPending=false;p.postalChoice=nullptr;p.controller.postalReady=false;
            for(auto& value:p.postalAuto)value="";
            keyboardDismiss(p);
            p.editorForm=nullptr;
            if (p.editor) {
                lv_obj_delete(p.editor);
                p.editor = nullptr;
            }
            p.editorLoaded = false;
            p.deleteConfirm = false;
            if (p.keyboard && lv_obj_is_valid(p.keyboard)) {
                lv_keyboard_set_textarea(p.keyboard, nullptr);
                lv_obj_add_flag(p.keyboard, LV_OBJ_FLAG_HIDDEN);
            }
        }
        void saveEditor(lv_event_t *event) {
            auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(event));
            if (!p.editorLoaded || p.editorReadOnly || p.controller.busy)
                return;
            const auto start = parseTime(lv_textarea_get_text(p.start)),
                       end = parseTime(lv_textarea_get_text(p.end));
            if (start <= 0 || end <= start) {
                lv_label_set_text(p.editorStatus,
                                  "Start/Ende prüfen (JJJJ-MM-TT HH:MM), auch Sommerzeit beachten");
                return;
            }
            JsonDocument args = p.draft;
            const unsigned cal = lv_dropdown_get_selected(p.editorCalendar);
            if (cal >= p.editCalendarCount) {
                lv_label_set_text(p.editorStatus, "Kein beschreibbarer Kalender ausgewählt");
                return;
            }
            args["calendar_id"] = p.editCalendars[cal];
            args["title"] = lv_textarea_get_text(p.editorTitle);
            args["start"] = start;
            args["end"] = end;
            args["all_day"] = bool(lv_obj_has_state(p.allDay, LV_STATE_CHECKED));
            if (args["all_day"] | false) {
                time_t begin = start, finish = end;
                tm a{}, b{};
                localtime_r(&begin, &a);
                localtime_r(&finish, &b);
                if (a.tm_hour || a.tm_min || b.tm_hour || b.tm_min) {
                    lv_label_set_text(p.editorStatus,
                                      "Ganztägig: Start und exklusives Ende auf 00:00 setzen");
                    return;
                }
            }
            args["location"] = lv_textarea_get_text(p.location);
            args["notes"] = lv_textarea_get_text(p.notes);
            static const char *addressKeys[] = {"street", "house_number", "postal_code", "city",
                                                "region", "country",      "phone"};
            for (unsigned i = 0; i < 7; ++i)
                args[addressKeys[i]] = lv_textarea_get_text(p.address[i]);
            uint16_t participants = 0;
            for (unsigned i = 0; i < p.controller.people.size() && i < 16; ++i)
                if (lv_obj_has_state(p.participants[i], LV_STATE_CHECKED))
                    participants |= p.controller.participantMasks[i];
            args["participants"] = participants;
            auto rules = args["rules"];
            rules["repeat"] = lv_dropdown_get_selected(p.repeat);
            rules["interval"] = lv_spinbox_get_value(p.interval);
            rules["count"] = lv_spinbox_get_value(p.count);
            const char *until = lv_textarea_get_text(p.until);
            const auto untilEpoch = *until ? parseTime(until) : 0;
            if (*until && !untilEpoch) {
                lv_label_set_text(p.editorStatus, "Wiederholungsende prüfen");
                return;
            }
            rules["until"] = untilEpoch;
            const auto reminderIndex = lv_dropdown_get_selected(p.reminder);
            rules["reminder_minutes"] = reminderIndex < std::size(reminderMinutes)
                                            ? reminderMinutes[reminderIndex]
                                            : (p.draft["rules"]["reminder_minutes"] | -1);
            if (!p.controller.action("save", args))
                lv_label_set_text(p.editorStatus, "System beschäftigt, bitte erneut speichern");
        }
        void deleteEditor(lv_event_t *event) {
            auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(event));
            if (p.editorReadOnly || p.controller.busy || p.newEvent)
                return;
            if (!p.deleteConfirm) {
                p.deleteConfirm = true;
                lv_label_set_text(p.editorStatus,
                                  "Noch einmal Löschen drücken, um den Termin zu entfernen");
                return;
            }
            JsonDocument args;
            args["id"] = p.draft["id"];
            args["revision"] = p.draft["revision"];
            if (lv_event_get_target(event) == p.removeOccurrence)
                args["occurrence"] = p.occurrence;
            p.controller.action("delete", args);
        }
        void fillEditor(NativeCalendarPage &p) {
            auto &c = p.controller;
            if (p.newEvent && !strtoul(c.details["calendar_id"] | "0", nullptr, 16)) {
                char id[9];
                snprintf(id, sizeof(id), "%08lx", (unsigned long)c.firstEditable());
                c.details["calendar_id"] = id;
            }
            p.draft = c.details;
            p.editorLoaded = true;
            const uint32_t id =
                static_cast<uint32_t>(strtoul(p.draft["calendar_id"] | "0", nullptr, 16));
            p.editorReadOnly = (p.draft["read_only"] | false) || !c.editable(id);
            lv_textarea_set_text(p.editorTitle, p.draft["title"] | "");
            char text[30];
            timeText(p.draft["start"] | int64_t(0), text, sizeof(text));
            lv_textarea_set_text(p.start, text);
            timeText(p.draft["end"] | int64_t(0), text, sizeof(text));
            lv_textarea_set_text(p.end, text);
            if (p.draft["all_day"] | false)
                lv_obj_add_state(p.allDay, LV_STATE_CHECKED);
            else
                lv_obj_remove_state(p.allDay, LV_STATE_CHECKED);
            lv_textarea_set_text(p.location, p.draft["location"] | "");
            lv_textarea_set_text(p.notes, p.draft["notes"] | "");
            static const char *addressKeys[] = {"street", "house_number", "postal_code", "city",
                                                "region", "country",      "phone"};
            for (unsigned i = 0; i < 7; ++i)
                lv_textarea_set_text(p.address[i], p.draft[addressKeys[i]] | "");
            String choices;
            unsigned selected = 0;
            p.editCalendarCount = 0;
            for (const auto &cal : c.calendars)
                if (c.editable(cal.id) || cal.id == id) {
                    if (choices.length())
                        choices += '\n';
                    choices += cal.name;
                    if (cal.id == id)
                        selected = p.editCalendarCount;
                    if (p.editCalendarCount < 32)
                        p.editCalendars[p.editCalendarCount++] = cal.id;
                }
            if (!choices.length())
                choices = "Kein beschreibbarer Kalender";
            lv_dropdown_set_options(p.editorCalendar, choices.c_str());
            lv_dropdown_set_selected(p.editorCalendar, selected);
            const uint16_t mask = p.draft["participants"] | uint16_t(0);
            for (unsigned i = 0; i < c.people.size() && i < 16; ++i) {
                lv_checkbox_set_text(p.participants[i], c.people[i].displayName);
                if (mask & c.participantMasks[i])
                    lv_obj_add_state(p.participants[i], LV_STATE_CHECKED);
                else
                    lv_obj_remove_state(p.participants[i], LV_STATE_CHECKED);
                lv_obj_remove_flag(p.participants[i], LV_OBJ_FLAG_HIDDEN);
                if(p.editorReadOnly) lv_obj_add_state(p.participants[i],LV_STATE_DISABLED);
                else lv_obj_remove_state(p.participants[i],LV_STATE_DISABLED);
            }
            auto rules = p.draft["rules"];
            lv_dropdown_set_selected(p.repeat, rules["repeat"] | 0u);
            unsigned reminderIndex = std::size(reminderMinutes);
            for (unsigned i = 0; i < std::size(reminderMinutes); ++i)
                if (reminderMinutes[i] == (rules["reminder_minutes"] | -1))
                    reminderIndex = i;
            lv_dropdown_set_options(p.reminder, reminderOptions);
            if (reminderIndex == std::size(reminderMinutes)) {
                String choices = reminderOptions;
                choices += '\n';
                choices += String(rules["reminder_minutes"] | -1) + " Minuten (vorhanden)";
                lv_dropdown_set_options(p.reminder, choices.c_str());
            }
            lv_dropdown_set_selected(p.reminder, reminderIndex);
            lv_spinbox_set_value(p.interval, rules["interval"] | 1);
            lv_spinbox_set_value(p.count, rules["count"] | 0);
            const auto until = rules["until"] | int64_t(0);
            if (until)
                timeText(until, text, sizeof(text));
            else
                text[0] = 0;
            lv_textarea_set_text(p.until, text);
            for (auto *obj : {p.save, p.remove, p.removeOccurrence}) {
                if (p.editorReadOnly ||
                    (!p.newEvent && obj == p.removeOccurrence && !(p.draft["recurring"] | false)) ||
                    (p.newEvent && obj != p.save))
                    lv_obj_add_state(obj, LV_STATE_DISABLED);
                else
                    lv_obj_remove_state(obj, LV_STATE_DISABLED);
            }
            for (auto *obj : {p.editorTitle, p.editorCalendar, p.start, p.end, p.allDay, p.location,
                              p.notes, p.repeat, p.interval, p.count, p.until, p.reminder}) {
                if (p.editorReadOnly)
                    lv_obj_add_state(obj, LV_STATE_DISABLED);
                else
                    lv_obj_remove_state(obj, LV_STATE_DISABLED);
            }
            for (auto *obj : p.address) {
                if (p.editorReadOnly)
                    lv_obj_add_state(obj, LV_STATE_DISABLED);
                else
                    lv_obj_remove_state(obj, LV_STATE_DISABLED);
            }
            lv_label_set_text(p.editorStatus,
                              p.editorReadOnly
                                  ? "Schreibgeschützt / Einstellungs-PIN nötig. Cloud-Termine "
                                    "werden nicht lokal überschrieben."
                                  : "Lokaler Termin · Änderungen werden atomar gespeichert");
        }
        void editor(NativeCalendarPage &p, bool fresh) {
            dismissEditor(p);
            p.draft.clear();
            p.editorReadOnly = true;
            p.newEvent = fresh;
            p.editor = lv_obj_create(p.root);
            UiTheme::instance().styleScreen(p.editor);
            lv_obj_set_size(p.editor, LV_PCT(100), LV_PCT(100));
            // A modal editor must not become another full-height flex row below
            // the existing calendar. Otherwise its lower fields get clipped.
            lv_obj_add_flag(p.editor,LV_OBJ_FLAG_FLOATING);
            lv_obj_align(p.editor,LV_ALIGN_CENTER,0,0);
            lv_obj_move_foreground(p.editor);
            lv_obj_set_style_pad_all(p.editor, 16, 0);
            lv_obj_set_flex_flow(p.editor, LV_FLEX_FLOW_COLUMN);
            lv_obj_clear_flag(p.editor, LV_OBJ_FLAG_SCROLLABLE);
            auto *tools = row(p.editor, 44);
            label(tools, fresh ? "Neuer Termin" : "Termindetails", 0, &hp_font_de_24);
            push(
                tools, "Schließen",
                [](lv_event_t *e) {
                    dismissEditor(*static_cast<NativeCalendarPage *>(lv_event_get_user_data(e)));
                },
                &p);
            push(
                tools, "PIN freigeben", [](lv_event_t *) { SecurityUi::requestSettingsUnlock(); },
                &p, 130);
            p.save = push(tools, "Speichern", saveEditor, &p);
            p.remove = push(tools, "Serie löschen", deleteEditor, &p, 135);
            p.removeOccurrence = push(tools, "Nur diesen", deleteEditor, &p, 120);
            p.editorStatus = label(p.editor, "Termindetails werden geladen …", 0, &hp_font_de_14);
            auto *form = content(p.editor, true);
            p.editorForm=form;
            lv_obj_set_height(form,0);
            auto *columns = row(form, 0);
            lv_obj_set_height(columns, LV_SIZE_CONTENT);
            lv_obj_set_flex_align(columns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_START);
            auto *left = content(columns, false);
            lv_obj_set_width(left, LV_PCT(48));
            lv_obj_set_flex_grow(left,0);
            lv_obj_set_height(left,LV_SIZE_CONTENT);
            auto *right = content(columns, false);
            lv_obj_set_width(right, LV_PCT(48));
            lv_obj_set_flex_grow(right,0);
            lv_obj_set_height(right,LV_SIZE_CONTENT);
            label(left,"Termin & Zeit",0,&hp_font_de_18);
            p.editorTitle = input(p, left, "Titel", 71);
            label(left, "Kalender", 0, &hp_font_de_14);
            p.editorCalendar = lv_dropdown_create(left);
            lv_obj_set_width(p.editorCalendar, LV_PCT(100));
            p.start = input(p, left, "Start · JJJJ-MM-TT HH:MM", 16);
            p.end = input(p, left, "Ende · JJJJ-MM-TT HH:MM (ganztägig: Folgetag 00:00)", 16);
            InputKeyboard::setMode(p.start,keyboardInput::Mode::DateTime);
            InputKeyboard::setMode(p.end,keyboardInput::Mode::DateTime);
            p.allDay = lv_checkbox_create(left);
            lv_checkbox_set_text(p.allDay, "Ganztägig");
            readableText(p.allDay, &hp_font_de_18);
            p.location = input(p, left, "Ort", 512);
            label(left,"Notizen",0,&hp_font_de_18);
            p.notes = input(p, left, "Beschreibung / zusätzliche Informationen", 4096, true);
            lv_obj_set_height(p.notes,160);
            label(right,"Adresse & Kontakt",0,&hp_font_de_18);
            static const char *captions[] = {"Straße", "Hausnummer", "Postleitzahl", "Stadt",
                                             "Region", "Land",       "Telefon"};
            static const size_t lengths[] = {192, 32, 32, 128, 128, 96, 96};
            for (unsigned i = 0; i < 7; ++i){
                p.address[i] = input(p, right, captions[i], lengths[i]);
                if(i==5)countryPicker(right,p.address[5]);
            }
            InputKeyboard::setMode(p.address[6],keyboardInput::Mode::Phone);
            label(right,"PLZ-Suche verwendet das Standardland der Firmware oder Ihre Auswahl.",0,&hp_font_de_14);
            p.postalChoice=lv_dropdown_create(right);lv_obj_add_flag(p.postalChoice,LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_event_cb(p.address[2],[](lv_event_t* e){
                auto& page=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));
                if(!page.editorLoaded||page.editorReadOnly||page.controller.busy)return;
                const String code=lv_textarea_get_text(page.address[2]);if(code.length()<2||code.length()>16)return;
                JsonDocument args;args["postal_code"]=code;args["country"]=String(lv_textarea_get_text(page.address[5]));
                page.controller.postalReady=false;page.postalPending=page.controller.action("postal_lookup",args);
                if(page.postalPending){page.postalCode=code;for(unsigned i=0;i<3;++i)page.postalBefore[i]=lv_textarea_get_text(page.address[i+3]);lv_obj_add_flag(page.postalChoice,LV_OBJ_FLAG_HIDDEN);}
            },LV_EVENT_DEFOCUSED,&p);
            lv_obj_add_event_cb(p.postalChoice,[](lv_event_t* e){
                auto& page=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));
                const unsigned selected=lv_dropdown_get_selected(page.postalChoice);if(!selected||!page.postalPending||page.postalCode!=lv_textarea_get_text(page.address[2])||page.postalBefore[2]!=lv_textarea_get_text(page.address[5]))return;
                auto place=page.controller.postal["places"][selected-1];const char* keys[]={"city","region","country"};
                for(unsigned i=0;i<3;++i){const String current=lv_textarea_get_text(page.address[i+3]);if(current==page.postalBefore[i]&&(!current.length()||current==page.postalAuto[i]||i==2)){lv_textarea_set_text(page.address[i+3],place[keys[i]]|"");page.postalAuto[i]=lv_textarea_get_text(page.address[i+3]);}}
                page.postalPending=false;
            },LV_EVENT_VALUE_CHANGED,&p);
            label(left, "Wiederholung", 0, &hp_font_de_14);
            p.repeat = lv_dropdown_create(left);
            lv_dropdown_set_options(p.repeat, "Keine\nTäglich\nWöchentlich\nMonatlich\nJährlich");
            label(left, "Intervall / Anzahl (0 = unbegrenzt)", 0, &hp_font_de_14);
            auto *rules = row(left, 48);
            const auto adjust = [](lv_event_t *e) {
                auto *box = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
                if (lv_obj_has_state(box, LV_STATE_DISABLED))
                    return;
                const char *caption =
                    lv_label_get_text(lv_obj_get_child(lv_event_get_target_obj(e), 0));
                if (caption[0] == '-')
                    lv_spinbox_decrement(box);
                else
                    lv_spinbox_increment(box);
            };
            p.interval = lv_spinbox_create(rules);
            lv_spinbox_set_range(p.interval, 1, 366);
            lv_spinbox_set_digit_format(p.interval, 3, 0);
            lv_obj_set_width(p.interval, 110);
            for (const char *sign : {"-", "+"}) {
                auto *control = push(rules, sign, adjust, p.interval, 40);
                lv_obj_add_event_cb(control, adjust, LV_EVENT_LONG_PRESSED_REPEAT, p.interval);
            }
            p.count = lv_spinbox_create(rules);
            lv_spinbox_set_range(p.count, 0, 10000);
            lv_spinbox_set_digit_format(p.count, 5, 0);
            lv_obj_set_width(p.count, 150);
            for (const char *sign : {"-", "+"}) {
                auto *control = push(rules, sign, adjust, p.count, 40);
                lv_obj_add_event_cb(control, adjust, LV_EVENT_LONG_PRESSED_REPEAT, p.count);
            }
            p.until = input(p, left, "Wiederholungsende (optional) · JJJJ-MM-TT HH:MM", 16);
            InputKeyboard::setMode(p.until,keyboardInput::Mode::DateTime);
            label(left, "Ausnahmen bleiben erhalten; einzelne Vorkommen separat löschen.", 0,
                  &hp_font_de_14);
            label(left, "Lokale Erinnerung (auf diesem Display)", 0, &hp_font_de_14);
            p.reminder = lv_dropdown_create(left);
            lv_dropdown_set_options(p.reminder, reminderOptions);
            label(right, "Lokale Teilnehmer", 0, &hp_font_de_18);
            if(p.controller.people.empty())
                label(right,"Noch keine Benutzer. Über Benutzer > + Benutzer anlegen.",0,&hp_font_de_14);
            label(right,"Die Auswahl ordnet Personen zu; es werden keine E-Mail-Einladungen versendet.",0,&hp_font_de_14);
            for (unsigned i = 0; i < 16; ++i) {
                p.participants[i] = lv_checkbox_create(right);
                lv_obj_add_flag(p.participants[i], LV_OBJ_FLAG_HIDDEN);
                readableText(p.participants[i], &hp_font_de_18);
            }
            if (fresh) {
                p.controller.details.clear();
                auto &d = p.controller.details;
                const auto start = calendar::localEpoch(p.controller.selected, 9),
                           end = calendar::localEpoch(p.controller.selected, 10);
                char id[9];
                snprintf(id, sizeof(id), "%08lx", (unsigned long)p.controller.firstEditable());
                d["calendar_id"] = id;
                d["start"] = start;
                d["end"] = end;
                d["title"] = "";
                d["revision"] = 0;
                fillEditor(p);
            } else if (p.controller.detailsReady)
                fillEditor(p);
            if (ensureKeyboard(p)) lv_obj_move_foreground(p.keyboard);
        }
        void filters(lv_event_t *event) {
            auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(event));
            auto &c = p.controller;
            const unsigned person = lv_dropdown_get_selected(p.person),
                           cal = lv_dropdown_get_selected(p.calendar);
            c.person = person && person <= c.people.size() ? c.people[person - 1].id : 0;
            c.calendarId = cal && cal <= c.calendars.size() ? c.calendars[cal - 1].id : 0;
            c.preset = lv_dropdown_get_selected(p.preset);
            JsonDocument args;
            args["person"] = c.person;
            args["calendar"] = c.calendarId;
            args["preset"] = c.preset;
            p.listOffset = 0;
            c.refresh(false);
            p.filtersPending = !c.action("native_view_save", args);
        }
        void poll(lv_timer_t *timer) {
            auto &p = *static_cast<NativeCalendarPage *>(lv_timer_get_user_data(timer));
            const int64_t minute = time(nullptr) / 60;
            if (p.lastMinute != minute) {
                p.lastMinute = minute;
                if (!p.settings && p.controller.mode != calendar::ViewMode::Month)
                    lv_obj_invalidate(p.board);
                if (!p.settings && p.drawnToday != dayOf(calendar::localDate(time(nullptr))))
                    p.controller.dirty = true;
            }
            p.controller.poll();
            if(p.filtersPending && !p.controller.busy) {
                JsonDocument args;
                args["person"]=p.controller.person;
                args["calendar"]=p.controller.calendarId;
                args["preset"]=p.controller.preset;
                p.filtersPending=!p.controller.action("native_view_save",args);
            }
            const bool unlocked = SecurityService::instance().settingsAllowed();
            if (p.unlocked != unlocked && !p.controller.busy) {
                p.unlocked = unlocked;
                JsonDocument args;
                p.controller.action("calendars", args);
                if (p.editor)
                    p.editorLoaded = false;
            }
            if(p.editor&&p.editorLoaded&&p.postalPending&&p.controller.postalReady){
                p.controller.postalReady=false;
                if(p.postalCode==lv_textarea_get_text(p.address[2])&&p.postalBefore[2]==lv_textarea_get_text(p.address[5])){
                    auto places=p.controller.postal["places"].as<JsonArray>();
                    if(places.size()==1){const char* keys[]={"city","region","country"};for(unsigned i=0;i<3;++i){const String current=lv_textarea_get_text(p.address[i+3]);if(current==p.postalBefore[i]&&(!current.length()||current==p.postalAuto[i]||i==2)){lv_textarea_set_text(p.address[i+3],places[0][keys[i]]|"");p.postalAuto[i]=lv_textarea_get_text(p.address[i+3]);}}p.postalPending=false;}
                    else{String options="Ort auswählen";for(auto place:places)options+="\n"+place["city"].as<String>()+" · "+place["region"].as<String>();lv_dropdown_set_options(p.postalChoice,options.c_str());lv_obj_remove_flag(p.postalChoice,LV_OBJ_FLAG_HIDDEN);}
                }else p.postalPending=false;
            }
            if (p.editor && p.controller.detailsReady && !p.editorLoaded && !p.controller.busy)
                fillEditor(p);
            if (p.editor && p.controller.actionDone) {
                dismissEditor(p);
                p.controller.actionDone = false;
            }
            if (p.controller.dirty || p.theme != UiTheme::instance().generation()) {
                p.controller.dirty = false;
                render(p);
                if (p.editor)
                    lv_label_set_text(p.editorStatus, p.controller.message.c_str());
            }
        }
        void settings(NativeCalendarPage &p) {
            auto *area = content(p.root, true);
            label(area, "Kalender & Familie", 0, &hp_font_de_28);
            label(area,
                  "Kalender, Personen, Geburtstage, Feiertagsregion und Cloud-Konten teilen "
                  "dieselbe Datenbasis.",
                  0, &hp_font_de_18);
            auto *tools = row(area, 48);
            push(
                tools, "Kalender öffnen",
                [](lv_event_t *) { UiShell::instance().requestAppOpen("calendar"); }, &p, 155);
            push(
                tools, "PIN freigeben", [](lv_event_t *) { SecurityUi::requestSettingsUnlock(); },
                &p, 145);
            push(
                tools, "Synchronisieren",
                [](lv_event_t *e) {
                    auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                    JsonDocument args;
                    p.controller.action("sync", args);
                },
                &p, 155);
            label(area,
                  "Konten und OAuth: bestehende Systemeinstellungen / "
                  "E-Mail-Konten.\nWeb-Verwaltung: /calendar auf dem vorhandenen Port-80-Server.",
                  0, &hp_font_de_18);
            auto *data = content(area, false);
            lv_obj_set_height(data, LV_SIZE_CONTENT);
            p.list = data;
        }
        void applyProfilePostal(NativeCalendarPage& p,unsigned index){
            if(p.profilePostalPerson!=p.profileEditingId||p.postalCode!=lv_textarea_get_text(p.profileFields[5])||p.postalBefore[2]!=lv_textarea_get_text(p.profileFields[8]))return;
            const auto place=p.controller.postal["places"][index];if(place.isNull())return;const char* keys[]={"city","region","country"};
            for(unsigned i=0;i<3;++i)if(p.postalBefore[i]==lv_textarea_get_text(p.profileFields[i+6]))lv_textarea_set_text(p.profileFields[i+6],place[keys[i]]|"");
            p.profileDirty=true;lv_label_set_text(p.status,"Ortsdaten ergänzt. Profil zum Übernehmen speichern.");
        }
        void buildPersonEditor(NativeCalendarPage& p) {
                auto* host=p.list;
                auto* tabs=row(host,44);
                for(unsigned i=0;i<3;++i){auto* button=push(tabs,i==0?"Profil":i==1?"Kontakt":"Verknüpfungen",[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));const auto index=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)));keyboardDismiss(p);for(unsigned n=0;n<3;++n){if(n==index)lv_obj_remove_flag(p.profileSections[n],LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(p.profileSections[n],LV_OBJ_FLAG_HIDDEN);}},&p,0);lv_obj_set_flex_grow(button,1);lv_obj_set_user_data(button,reinterpret_cast<void*>(i));}
                for(unsigned i=0;i<3;++i){p.profileSections[i]=content(host,true);lv_obj_set_width(p.profileSections[i],LV_PCT(100));lv_obj_set_height(p.profileSections[i],0);lv_obj_set_flex_grow(p.profileSections[i],1);if(i)lv_obj_add_flag(p.profileSections[i],LV_OBJ_FLAG_HIDDEN);}
                p.list=p.profileSections[0];
                label(p.list, "Benutzerprofil", 0, &hp_font_de_24);
                p.personChoice = lv_dropdown_create(p.list);
                lv_obj_set_width(p.personChoice, 400);
                lv_obj_add_flag(p.personChoice,LV_OBJ_FLAG_HIDDEN);
                p.personName = input(p, p.list, "Vorname", 39);
                p.personLastName = input(p, p.list, "Nachname", 39);
                p.profileFields[0]=input(p,p.list,"Anzeigename (optional)",39);
                label(p.list,"Profilfarbe",0,&hp_font_de_14);p.profileColor=lv_dropdown_create(p.list);lv_dropdown_set_options(p.profileColor,"Blau\nRot\nGrün\nViolett\nOrange\nTürkis\nRosa\nGelb\nStahlblau\nLimette\nBeere\nIndigo\nSand\nMint\nKoralle\nLavendel");
                p.personEmail = input(p, p.list, "E-Mail-Adresse (optional)", 63);
                p.birthday =
                    input(p, p.list, "Geburtstag; Jahr 0000 = unbekannt", 10);
                lv_textarea_set_accepted_chars(p.birthday,"0123456789-");
                lv_obj_add_event_cb(p.birthday,[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));if(p.formattingBirthday)return;
                    const char* input=lv_textarea_get_text(p.birthday);const auto cursor=lv_textarea_get_cursor_pos(p.birthday);unsigned digits=0;
                    for(unsigned i=0;input[i]&&i<cursor;++i)if(input[i]>='0'&&input[i]<='9')++digits;
                    const auto text=calendar::birthdayInput(input,p.controller.dateGerman);
                    if(text!=input){unsigned position=0,count=0;while(position<text.size()&&count<digits){if(text[position]>='0'&&text[position]<='9')++count;++position;}
                        p.formattingBirthday=true;lv_textarea_set_text(p.birthday,text.c_str());lv_textarea_set_cursor_pos(p.birthday,position);p.formattingBirthday=false;}
                },LV_EVENT_VALUE_CHANGED,&p);
                label(p.list,"Datumsformat · für alle",0,&hp_font_de_14);
                p.dateFormat=lv_dropdown_create(p.list);lv_dropdown_set_options(p.dateFormat,"Englisch / ISO: JJJJ-MM-TT\nDeutsch: TT-MM-JJJJ");lv_obj_set_width(p.dateFormat,400);
                lv_obj_add_event_cb(p.dateFormat,[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));JsonDocument args;args["date_format"]=lv_dropdown_get_selected(p.dateFormat)?"de":"iso";
                    if(!p.controller.action("person_preferences",args)){lv_dropdown_set_selected(p.dateFormat,p.controller.dateGerman?1:0);lv_label_set_text(p.status,"Bitte laufenden Vorgang abwarten");}
                },LV_EVENT_VALUE_CHANGED,&p);
                p.profileCreateCalendar=lv_checkbox_create(p.list);lv_checkbox_set_text(p.profileCreateCalendar,"Persönlichen Kalender automatisch anlegen");lv_obj_add_state(p.profileCreateCalendar,LV_STATE_CHECKED);
                p.list=p.profileSections[2];
                label(p.list,"Vorhandenen Kontakt verknüpfen (sonst automatisch neu)",0,&hp_font_de_14);p.personContact=lv_dropdown_create(p.list);lv_dropdown_set_options(p.personContact,"Automatisch verknüpfen / anlegen");lv_obj_set_width(p.personContact,LV_PCT(100));
                lv_obj_add_event_cb(p.personContact,[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));p.contactChanged=true;const unsigned index=lv_dropdown_get_selected(p.personContact);p.profileContactRequest=index?p.controller.contacts["items"][index-1]["id"].as<String>():String();p.profileApplied=0;p.profileRequested=0;p.profileRetryAt=0;},LV_EVENT_VALUE_CHANGED,&p);
                lv_obj_add_event_cb(
                    p.personChoice,
                    [](lv_event_t *event) {
                        auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(event));
                        const unsigned index = lv_dropdown_get_selected(p.personChoice);
                        p.controller.dirty=true;p.profileSignature="";
                        p.profilePopulating=true;p.profileDirty=false;
                        p.contactChanged=false;
                        p.profileRequested=p.profileApplied=0;p.profileDeleteArmed=false;
                        p.profileEditingId=0;p.profileRequestedGeneration=0;
                        p.profileContactRequest="";
                        p.profileRequestToken="";
                        p.postalPending=false;if(p.profilePostalChoice)lv_obj_add_flag(p.profilePostalChoice,LV_OBJ_FLAG_HIDDEN);
                        for(auto* field:p.profileFields)if(field)lv_textarea_set_text(field,"");
                        if (!index) {
                            lv_obj_add_state(p.profileCreateCalendar,LV_STATE_CHECKED);
                            lv_textarea_set_text(p.personName, "");
                            lv_dropdown_set_selected(p.profileColor,0);
                            lv_textarea_set_text(p.personLastName, "");
                            lv_textarea_set_text(p.personEmail, "");
                            lv_textarea_set_text(p.birthday, "");
                            lv_dropdown_set_selected(p.personContact,0);
                            p.profilePopulating=false;return;
                        }
                        if (index > p.controller.people.size())
                            return;
                        const auto &person = p.controller.people[index - 1];
                        p.profileEditingId=person.id;
                        char nameId[9];snprintf(nameId,sizeof(nameId),"%08lx",static_cast<unsigned long>(person.id));
                        lv_textarea_set_text(p.personName,p.controller.personNames[nameId]["first"]|person.displayName);
                        lv_textarea_set_text(p.personLastName,p.controller.personNames[nameId]["last"]|"");
                        lv_dropdown_set_selected(p.profileColor,person.colorIndex);
                        lv_textarea_set_text(p.personEmail,p.controller.personNames[nameId]["contact_email"]|(String(person.email).endsWith("@calendar.local")?"":person.email));
                        const auto text=calendar::birthdayText(person.birthdayYear,person.birthdayMonth,person.birthdayDay,p.controller.dateGerman);
                        lv_textarea_set_text(p.birthday, text.c_str());
                        char id[9];snprintf(id,sizeof(id),"%08lx",static_cast<unsigned long>(person.id));
                        const String link=p.controller.personLinks[id]|"";unsigned selected=0,i=1;
                        for(auto contact:p.controller.contacts["items"].as<JsonArray>()){if(contact["id"]==link)selected=i;++i;}lv_dropdown_set_selected(p.personContact,selected);
                        p.profilePopulating=false;
                    },
                    LV_EVENT_VALUE_CHANGED, &p);
                label(p.list,"Kalender für diesen Benutzer · höchstens 4",0,&hp_font_de_18);
                for(unsigned i=0;i<32;++i){p.profileCalendars[i]=lv_checkbox_create(p.list);lv_obj_add_flag(p.profileCalendars[i],LV_OBJ_FLAG_HIDDEN);readableText(p.profileCalendars[i],&hp_font_de_18);lv_obj_add_event_cb(p.profileCalendars[i],[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));unsigned checked=0;for(auto* check:p.profileCalendars)checked+=lv_obj_has_state(check,LV_STATE_CHECKED);if(checked>4){lv_obj_remove_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);lv_label_set_text(p.status,"Bitte höchstens vier Kalender auswählen.");}},LV_EVENT_VALUE_CHANGED,&p);}
                label(p.list,"Verknüpfte Kalenderkonten",0,&hp_font_de_18);
                for(unsigned i=0;i<5;++i){p.profileAccounts[i]=lv_checkbox_create(p.list);lv_obj_add_flag(p.profileAccounts[i],LV_OBJ_FLAG_HIDDEN);readableText(p.profileAccounts[i],&hp_font_de_18);lv_obj_set_user_data(p.profileAccounts[i],reinterpret_cast<void*>(i));lv_obj_add_event_cb(p.profileAccounts[i],[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));const auto selected=lv_dropdown_get_selected(p.personChoice);if(!selected||p.profileDirty||p.controller.busy||selected>p.controller.people.size()){lv_obj_remove_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);lv_label_set_text(p.status,"Benutzer zuerst speichern; laufenden Vorgang abwarten.");return;}JsonDocument args;args["slot"]=unsigned(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e))));args["person_id"]=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED)?p.controller.people[selected-1].id:0u;if(!p.controller.action("account_person",args))lv_label_set_text(p.status,"Zuordnung nicht gestartet.");},LV_EVENT_VALUE_CHANGED,&p);}
                p.list=host;
                auto* footer=row(host,52);
                p.profileSave=push(
                    footer, "Speichern",
                    [](lv_event_t *event) {
                        auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(event));
                        JsonDocument args;
                        args["name"] = lv_textarea_get_text(p.personName);
                        args["firstName"] = lv_textarea_get_text(p.personName);
                        args["lastName"] = lv_textarea_get_text(p.personLastName);
                        args["color"]=lv_dropdown_get_selected(p.profileColor);
                        static const char* profileKeys[]={"display_name","phone","mobile","street","houseNumber","postalCode","city","region","country"};
                        for(unsigned i=0;i<9;++i)if(p.profileFields[i])args[profileKeys[i]]=lv_textarea_get_text(p.profileFields[i]);
                        if(!String(args["display_name"]|"").length())args.remove("display_name");
                        args["create_calendar"]=lv_obj_has_state(p.profileCreateCalendar,LV_STATE_CHECKED);
                        if(!p.profileEditingId){if(!p.profileRequestToken.length()){char token[17];snprintf(token,sizeof(token),"%08lx%08lx",static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()));p.profileRequestToken=token;}args["request_id"]=p.profileRequestToken;}
                        auto views=args["calendars"].to<JsonArray>();for(unsigned i=0;i<32;++i)if(p.profileCalendarIds[i]&&lv_obj_has_state(p.profileCalendars[i],LV_STATE_CHECKED)){char id[9];snprintf(id,sizeof(id),"%08lx",static_cast<unsigned long>(p.profileCalendarIds[i]));views.add(String(id));}
                        args["contact_email"] = lv_textarea_get_text(p.personEmail);args["email"]="";
                        const auto contact=lv_dropdown_get_selected(p.personContact);
                        if(p.contactChanged&&!p.controller.contacts["items"].isNull()){
                            args["contact_id"]=contact?p.controller.contacts["items"][contact-1]["id"].as<String>():String();
                        }
                        const unsigned selected=lv_dropdown_get_selected(p.personChoice);
                        if(p.profileEditingId)args["id"]=p.profileEditingId;
                        if(p.profileEditingId&&p.profileEditingRevision)args["revision"]=p.profileEditingRevision;
                        if(selected&&selected<=p.controller.people.size()&&String(p.controller.people[selected-1].email).endsWith("@calendar.local"))args["email"]="";
                        const char *birthday = lv_textarea_get_text(p.birthday);
                        args["clear_birthday"]=!*birthday;
                        if (*birthday) {
                            int year, month, day;
                            if (!calendar::parseBirthday(birthday,p.controller.dateGerman,year,month,day) ||
                                !calendar::validDate(year ? year : 2000, month, day)) {
                                lv_label_set_text(p.status, "Geburtstagsdatum prüfen");
                                return;
                            }
                            args["birthday_year"] = year;
                            args["birthday_month"] = month;
                            args["birthday_day"] = day;
                        }
                        if(p.controller.action("person_save", args))enableProfile(p,false);
                    },
                    &p, 185);
                p.profileDelete=push(footer,"Löschen",[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));const unsigned selected=lv_dropdown_get_selected(p.personChoice);if(p.controller.busy||!selected||selected>p.controller.people.size())return;
                    if(p.profileDirty){lv_label_set_text(p.status,"Änderungen zuerst speichern oder verwerfen.");return;}
                    if(!p.profileDeleteArmed){p.profileDeleteArmed=true;lv_label_set_text(p.status,"Benutzer löschen? Kontakt bleibt erhalten. Erneut Löschen zum Bestätigen drücken.");return;}
                    JsonDocument args;args["id"]=p.controller.people[selected-1].id;args["confirmed"]=true;if(p.controller.action("person_remove",args)){p.profileDeleteArmed=false;p.profileRequested=p.profileApplied=0;lv_dropdown_set_selected(p.personChoice,0);}
                },&p,150);
                p.list=p.profileSections[1];
                const char* titles[]={"Telefon","Mobilnummer","Straße","Hausnummer","Postleitzahl","Ort","Region","Land"};
                const unsigned limits[]={96,96,192,32,32,128,96,96};
                for(unsigned i=0;i<8;++i)p.profileFields[i+1]=input(p,p.list,titles[i],limits[i]);
                p.profileCountryPicker=countryPicker(p.list,p.profileFields[8]);
                p.profilePostalChoice=lv_dropdown_create(p.list);lv_obj_set_width(p.profilePostalChoice,LV_PCT(100));lv_obj_add_flag(p.profilePostalChoice,LV_OBJ_FLAG_HIDDEN);lv_obj_add_event_cb(p.profilePostalChoice,[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));const unsigned choice=lv_dropdown_get_selected(p.profilePostalChoice);if(choice)applyProfilePostal(p,choice-1);},LV_EVENT_VALUE_CHANGED,&p);
                push(p.list,"PLZ / Ort ergänzen",[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));JsonDocument args;args["postal_code"]=lv_textarea_get_text(p.profileFields[5]);args["country"]=lv_textarea_get_text(p.profileFields[8]);if(p.controller.action("postal_lookup",args)){p.postalPending=true;p.controller.postalReady=false;p.postalCode=lv_textarea_get_text(p.profileFields[5]);p.profilePostalPerson=p.profileEditingId;for(unsigned i=0;i<3;++i)p.postalBefore[i]=lv_textarea_get_text(p.profileFields[i+6]);}},&p,230);
                for(unsigned group=0;group<2;++group){
                    auto* parent=p.profileSections[group];lv_obj_set_flex_flow(parent,LV_FLEX_FLOW_ROW_WRAP);
                    std::vector<lv_obj_t*> fields;
                    if(group==0)fields={p.personName,p.personLastName,p.profileFields[0],p.profileColor,p.personEmail,p.birthday,p.dateFormat};
                    else for(unsigned i=1;i<9;++i)fields.push_back(p.profileFields[i]);
                    for(auto* field:fields){auto* label=lv_obj_get_child(parent,lv_obj_get_index(field)-1);auto* cell=content(parent,false);lv_obj_set_width(cell,LV_PCT(48));lv_obj_set_height(cell,LV_SIZE_CONTENT);lv_obj_set_style_pad_all(cell,0,0);lv_obj_set_parent(label,cell);lv_obj_set_parent(field,cell);lv_obj_set_width(field,LV_PCT(100));if(field==p.profileFields[8])lv_obj_set_parent(p.profileCountryPicker,cell);}
                }
                p.list=host;
                for(auto* field:p.profileFields)lv_obj_add_event_cb(field,[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));if(!p.profilePopulating&&lv_indev_active())p.profileDirty=true;},LV_EVENT_VALUE_CHANGED,&p);
                for(auto* obj:{p.personName,p.personLastName,p.personEmail,p.birthday,p.profileColor,p.personContact})lv_obj_add_event_cb(obj,[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));if(!p.profilePopulating&&lv_indev_active())p.profileDirty=true;},LV_EVENT_VALUE_CHANGED,&p);
                for(auto* obj:p.profileCalendars)lv_obj_add_event_cb(obj,[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));if(lv_indev_active())p.profileDirty=true;},LV_EVENT_VALUE_CHANGED,&p);
        }
        void enableProfile(NativeCalendarPage& p,bool enabled){for(auto* obj:{p.personName,p.personLastName,p.personEmail,p.birthday,p.profileColor,p.personContact}){if(enabled)lv_obj_remove_state(obj,LV_STATE_DISABLED);else lv_obj_add_state(obj,LV_STATE_DISABLED);}for(auto* obj:p.profileCalendars){if(enabled)lv_obj_remove_state(obj,LV_STATE_DISABLED);else lv_obj_add_state(obj,LV_STATE_DISABLED);}}
        void chooseProfile(NativeCalendarPage& p,unsigned selected,const char* destination){
            if(p.controller.busy||p.profileConfirm)return;
            if(!p.profileDirty){if(*destination){UiShell::instance().requestAppOpen(destination);return;}lv_dropdown_set_selected(p.personChoice,selected);lv_obj_send_event(p.personChoice,LV_EVENT_VALUE_CHANGED,nullptr);return;}
            p.pendingProfile=selected;p.pendingProfileDestination=destination;
            p.profileConfirm=lv_obj_create(p.root);lv_obj_add_flag(p.profileConfirm,LV_OBJ_FLAG_FLOATING);lv_obj_set_size(p.profileConfirm,LV_PCT(100),LV_PCT(100));lv_obj_set_style_bg_color(p.profileConfirm,lv_color_black(),0);lv_obj_set_style_bg_opa(p.profileConfirm,LV_OPA_50,0);lv_obj_center(p.profileConfirm);
            auto* box=lv_msgbox_create(p.profileConfirm);lv_obj_set_style_text_font(box,&hp_font_de_18,0);lv_obj_set_width(box,520);lv_obj_center(box);lv_msgbox_add_title(box,"Ungespeicherte Änderungen");lv_msgbox_add_text(box,"Änderungen verwerfen? Mit Abbrechen kannst du sie zuerst speichern.");
            auto* discard=lv_msgbox_add_footer_button(box,"Verwerfen");lv_obj_add_event_cb(discard,[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));if(p.controller.busy){lv_label_set_text(p.status,"Bitte laufenden Vorgang abwarten.");return;}auto* overlay=p.profileConfirm;p.profileConfirm=nullptr;lv_obj_delete(overlay);p.profileDirty=false;chooseProfile(p,p.pendingProfile,p.pendingProfileDestination.c_str());},LV_EVENT_CLICKED,&p);
            auto* cancel=lv_msgbox_add_footer_button(box,"Abbrechen");lv_obj_add_event_cb(cancel,[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));auto* overlay=p.profileConfirm;p.profileConfirm=nullptr;lv_obj_delete(overlay);},LV_EVENT_CLICKED,&p);
        }
        void renderUsersList(NativeCalendarPage& p){
            if(!p.userList||!p.personChoice)return;
            String query=lv_textarea_get_text(p.userSearch);query.trim();
            String signature=query+":"+String(lv_dropdown_get_selected(p.personChoice));for(const auto& person:p.controller.people)signature+="|"+String(person.id)+":"+person.displayName;
            if(signature==p.usersSignature&&lv_obj_get_child_count(p.userList))return;p.usersSignature=signature;lv_obj_clean(p.userList);
            unsigned shown=0;
            std::vector<unsigned> order;for(unsigned i=0;i<p.controller.people.size();++i)order.push_back(i);std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return phonebookNameLess(p.controller.people[a].displayName,p.controller.people[b].displayName);});
            for(unsigned i:order){const auto& person=p.controller.people[i];if(!phonebookContains(person.displayName,query.c_str()))continue;
                auto* button=push(p.userList,person.displayName,[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));if(p.controller.busy)return;const unsigned selected=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)));if(selected>p.controller.people.size())return;chooseProfile(p,selected);},&p,300);
                lv_obj_set_width(button,LV_PCT(100));lv_obj_set_user_data(button,reinterpret_cast<void*>(i+1));++shown;
                if(lv_dropdown_get_selected(p.personChoice)==i+1)lv_obj_add_state(button,LV_STATE_CHECKED);
            }
            if(!shown)label(p.userList,p.controller.people.empty()?"Noch keine Benutzer. Mit + Benutzer beginnen.":"Keine passenden Benutzer.",0,&hp_font_de_18);
        }
        void renderPersonEditor(NativeCalendarPage& p) {
            auto& c=p.controller;
            if(p.profileEditingId&&!c.busy&&!p.profileDirty&&std::none_of(c.people.begin(),c.people.end(),[&](const auto& person){return person.id==p.profileEditingId;})){lv_dropdown_set_selected(p.personChoice,0);lv_obj_send_event(p.personChoice,LV_EVENT_VALUE_CHANGED,nullptr);lv_label_set_text(p.status,"Benutzer wurde entfernt. Kontakte bleiben erhalten.");}
            if(p.profileCreateCalendar){if(p.profileEditingId)lv_obj_add_flag(p.profileCreateCalendar,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(p.profileCreateCalendar,LV_OBJ_FLAG_HIDDEN);}
            if(p.profileContactRequest.length()&&!c.busy&&int32_t(millis()-p.profileRetryAt)>=0&&c.profileDetails["contact_id"]!=p.profileContactRequest){JsonDocument args;args["contact_id"]=p.profileContactRequest;if(c.action("user_profile",args)){c.profileDetails.clear();p.profileRetryAt=millis()+5000;}}
            if(!p.profileContactRequest.length()&&p.profileEditingId&&!c.busy&&!p.profileDirty&&(p.profileRequested!=p.profileEditingId||p.profileRequestedGeneration!=c.peopleGeneration||(!p.profileApplied&&int32_t(millis()-p.profileRetryAt)>=0))){
                JsonDocument args;args["id"]=p.profileEditingId;if(c.action("user_profile",args)){c.profileDetails.clear();p.profileRequested=p.profileEditingId;p.profileRequestedGeneration=c.peopleGeneration;p.profileApplied=0;p.profileRetryAt=millis()+5000;}
            }
            if(!c.busy&&!p.profileApplied&&c.profileDetails["id"].is<const char*>()&&(!p.profileDirty||p.profileContactRequest.length())){
                char id[9];snprintf(id,sizeof(id),"%08lx",static_cast<unsigned long>(p.profileRequested));
                const bool contactReply=p.profileContactRequest.length()&&c.profileDetails["contact_id"]==p.profileContactRequest;
                if(contactReply||(p.profileRequested&&c.profileDetails["id"]==id)){p.profilePopulating=true;auto contact=c.profileDetails["contact"];static const char* keys[]={"phone","mobile","street","houseNumber","postalCode","city","country"};
                    for(unsigned i=0;i<7;++i){const unsigned index=i==6?8:i+1;lv_textarea_set_text(p.profileFields[index],contact[keys[i]]|"");}
                    lv_textarea_set_text(p.profileFields[7],contact["addresses"][0]["region"]|"");
                    for(const auto& person:c.people)if(person.id==p.profileRequested){const String first=c.personNames[id]["first"]|person.displayName,last=c.personNames[id]["last"]|"";lv_textarea_set_text(p.profileFields[0],String(person.displayName)==personDisplayName(first,last)?"":person.displayName);lv_textarea_set_text(p.personName,c.personNames[id]["first"]|person.displayName);lv_textarea_set_text(p.personLastName,c.personNames[id]["last"]|"");lv_textarea_set_text(p.personEmail,c.personNames[id]["contact_email"]|"");lv_dropdown_set_selected(p.profileColor,person.colorIndex);const auto birthday=calendar::birthdayText(person.birthdayYear,person.birthdayMonth,person.birthdayDay,c.dateGerman);lv_textarea_set_text(p.birthday,birthday.c_str());}
                    p.profileApplied=p.profileRequested;p.profilePopulating=false;
                    if(!contactReply)p.profileEditingRevision=c.profileDetails["revision"]|0u;
                    if(contactReply){p.profilePopulating=true;if(!p.profileEditingId){if(!strlen(lv_textarea_get_text(p.personName)))lv_textarea_set_text(p.personName,contact["firstName"]|(contact["name"]|""));if(!strlen(lv_textarea_get_text(p.personLastName)))lv_textarea_set_text(p.personLastName,contact["lastName"]|"");}lv_textarea_set_text(p.personEmail,contact["email"]|"");ContactProtocol::Birthday birth;if(ContactProtocol::parseBirthday(contact["birthday"]|"",birth)){const auto value=calendar::birthdayText(birth.year,birth.month,birth.day,c.dateGerman);lv_textarea_set_text(p.birthday,value.c_str());}p.profileContactRequest="";p.profileApplied=p.profileEditingId;p.profilePopulating=false;p.profileDirty=true;}
                }
            }
            if(p.postalPending&&c.postalReady&&!p.editor&&p.profilePostalPerson==p.profileEditingId&&p.postalCode==lv_textarea_get_text(p.profileFields[5])){c.postalReady=false;p.postalPending=false;
                if(c.postal["places"].size()==1)applyProfilePostal(p,0);
                else if(c.postal["places"].size()>1){String options="Ort auswählen";for(auto place:c.postal["places"].as<JsonArray>())options+=String("\n")+(place["city"]|"")+" · "+(place["region"]|"");lv_dropdown_set_options(p.profilePostalChoice,options.c_str());lv_obj_remove_flag(p.profilePostalChoice,LV_OBJ_FLAG_HIDDEN);}
                else lv_label_set_text(p.status,"Keine eindeutigen Ortsdaten. Manuelle Eingabe bleibt möglich.");
            }
            String names = "Neu / keine Person";
            if(p.dateFormat){
                if(p.dateGermanShown!=c.dateGerman){int year,month,day;const char* current=lv_textarea_get_text(p.birthday);
                    if(calendar::parseBirthday(current,p.dateGermanShown,year,month,day)){const auto text=calendar::birthdayText(year,month,day,c.dateGerman);p.formattingBirthday=true;lv_textarea_set_text(p.birthday,text.c_str());p.formattingBirthday=false;}
                    p.dateGermanShown=c.dateGerman;
                }
                if(!c.busy)lv_dropdown_set_selected(p.dateFormat,c.dateGerman?1:0);
                lv_textarea_set_placeholder_text(p.birthday,c.dateGerman?"TT-MM-JJJJ":"JJJJ-MM-TT");
            }
            if(!c.busy&&!p.profileDirty&&!p.profileContactRequest.length()&&p.profileContactsGeneration!=c.peopleGeneration){p.profileContactsGeneration=c.peopleGeneration;c.contacts.clear();p.contactsRequested=false;}
            if(!c.busy&&SecurityService::instance().settingsAllowed()&&(!p.contactsRequested||(c.contacts["more"]|false))){
                JsonDocument args;args["offset"]=c.contacts["next_offset"]|0;p.contactsRequested=c.action("person_contacts",args);
            }
            if(p.personContact&&!c.contacts["items"].isNull()){
                String options="Automatisch verknüpfen";for(auto contact:c.contacts["items"].as<JsonArray>())options+=String("\n")+(contact["name"]|"");
                if(options!=lv_dropdown_get_options(p.personContact)){
                    const auto previous=lv_dropdown_get_selected(p.personContact);String link;
                    if(p.contactChanged&&previous)link=c.contacts["items"][previous-1]["id"].as<String>();
                    else if(!p.contactChanged){const auto person=lv_dropdown_get_selected(p.personChoice);if(person&&person<=c.people.size()){char id[9];snprintf(id,sizeof(id),"%08lx",static_cast<unsigned long>(c.people[person-1].id));link=c.personLinks[id]|"";}}
                    lv_dropdown_set_options(p.personContact,options.c_str());unsigned index=1,selected=0;
                    for(auto contact:c.contacts["items"].as<JsonArray>()){if(contact["id"]==link)selected=index;++index;}lv_dropdown_set_selected(p.personContact,selected);
                }
            }
            for (const auto &person : c.people)
                names += "\n" + String(person.displayName);
            if (names != lv_dropdown_get_options(p.personChoice)){unsigned selected=0;for(unsigned i=0;i<c.people.size();++i)if(c.people[i].id==p.profileEditingId)selected=i+1;lv_dropdown_set_options(p.personChoice,names.c_str());lv_dropdown_set_selected(p.personChoice,selected);}
            if(c.savedPersonId){for(unsigned i=0;i<c.people.size();++i)if(c.people[i].id==c.savedPersonId){c.savedPersonId=0;lv_dropdown_set_selected(p.personChoice,i+1);lv_obj_send_event(p.personChoice,LV_EVENT_VALUE_CHANGED,nullptr);break;}}
            const unsigned selected=lv_dropdown_get_selected(p.personChoice);const uint32_t personId=selected&&selected<=c.people.size()?c.people[selected-1].id:0;
            String signature=String(personId)+":"+String(c.peopleGeneration);for(const auto& cal:c.calendars)signature+="|"+String(cal.id)+":"+cal.name;
            if(signature!=p.profileSignature&&!p.profileDirty){p.profileSignature=signature;char key[9];snprintf(key,sizeof(key),"%08lx",static_cast<unsigned long>(personId));
                for(unsigned i=0;i<32;++i){auto* check=p.profileCalendars[i];if(i>=c.calendars.size()){p.profileCalendarIds[i]=0;lv_obj_add_flag(check,LV_OBJ_FLAG_HIDDEN);continue;}const auto& cal=c.calendars[i];p.profileCalendarIds[i]=cal.id;lv_checkbox_set_text(check,cal.name);lv_obj_remove_flag(check,LV_OBJ_FLAG_HIDDEN);bool checked=false;char id[9];snprintf(id,sizeof(id),"%08lx",static_cast<unsigned long>(cal.id));for(auto value:c.personNames[key]["calendars"].as<JsonArray>())checked|=value==id;if(checked)lv_obj_add_state(check,LV_STATE_CHECKED);else lv_obj_remove_state(check,LV_STATE_CHECKED);}
            }
            const String accountSignature=String(personId)+":"+String(c.statusGeneration);
            if(accountSignature!=p.profileAccountsSignature){p.profileAccountsSignature=accountSignature;char key[9];snprintf(key,sizeof(key),"%08lx",static_cast<unsigned long>(personId));
                for(auto* check:p.profileAccounts)lv_obj_add_flag(check,LV_OBJ_FLAG_HIDDEN);
                for(auto account:c.status["accounts"].as<JsonArray>()){const unsigned slot=account["slot"]|255u;if(slot>=5)continue;auto* check=p.profileAccounts[slot];lv_checkbox_set_text(check,account["email"]|"Kalenderkonto");lv_obj_remove_flag(check,LV_OBJ_FLAG_HIDDEN);if(personId&&account["person_id"]==key)lv_obj_add_state(check,LV_STATE_CHECKED);else lv_obj_remove_state(check,LV_STATE_CHECKED);}
            }
            const bool ready=!c.busy&&!p.profileContactRequest.length()&&SecurityService::instance().settingsAllowed()&&(!p.profileEditingId||p.profileApplied==p.profileEditingId);
            enableProfile(p,ready);
            for(auto* field:p.profileFields){if(ready)lv_obj_remove_state(field,LV_STATE_DISABLED);else lv_obj_add_state(field,LV_STATE_DISABLED);}
            for(auto* control:{p.profileSave,p.profileCreateCalendar,p.profileDelete}){if(ready)lv_obj_remove_state(control,LV_STATE_DISABLED);else lv_obj_add_state(control,LV_STATE_DISABLED);}
        }
        void renderSettings(NativeCalendarPage &p) {
            auto &c = p.controller;
            if(p.usersOnly){if(!p.settingsBuilt){p.settingsBuilt=true;buildPersonEditor(p);}renderPersonEditor(p);renderUsersList(p);return;}
            if (c.calendars.empty())
                return;
            if (!p.settingsBuilt) {
                p.settingsBuilt = true;
                label(p.list,"Gemeinsame Funktionen · für alle",0,&hp_font_de_24);
                for(unsigned i=0;i<2;++i){p.sharedFeature[i]=lv_checkbox_create(p.list);readableText(p.sharedFeature[i],&hp_font_de_18);lv_obj_add_flag(p.sharedFeature[i],LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_event_cb(p.sharedFeature[i],[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));unsigned i=0;while(i<2&&p.sharedFeature[i]!=lv_event_get_target(e))++i;if(i>=p.controller.features.size())return;JsonDocument args;char id[9];snprintf(id,sizeof(id),"%08lx",static_cast<unsigned long>(p.controller.features[i].id));args["calendar_id"]=String(id);args["visible"]=lv_obj_has_state(p.sharedFeature[i],LV_STATE_CHECKED);if(!p.controller.action("visibility",args))lv_label_set_text(p.status,"Bitte laufenden Vorgang abwarten");},LV_EVENT_VALUE_CHANGED,&p);}
                label(p.list, "Sichtbare Kalender", 0, &hp_font_de_24);
                for (unsigned i = 0; i < 32; ++i) {
                    p.visibility[i] = lv_checkbox_create(p.list);
                    readableText(p.visibility[i], &hp_font_de_18);
                    lv_obj_add_flag(p.visibility[i], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_event_cb(
                        p.visibility[i],
                        [](lv_event_t *event) {
                            auto &p =
                                *static_cast<NativeCalendarPage *>(lv_event_get_user_data(event));
                            unsigned index = 0;
                            while (index < 32 && p.visibility[index] != lv_event_get_target(event))
                                ++index;
                            if (index >= p.controller.calendars.size())
                                return;
                            JsonDocument args;
                            args["calendar_id"] = p.controller.calendars[index].id;
                            args["visible"] =
                                bool(lv_obj_has_state(p.visibility[index], LV_STATE_CHECKED));
                            if (!p.controller.action("visibility", args)) {
                                if (p.controller.calendars[index].enabled)
                                    lv_obj_add_state(p.visibility[index], LV_STATE_CHECKED);
                                else
                                    lv_obj_remove_state(p.visibility[index], LV_STATE_CHECKED);
                            }
                        },
                        LV_EVENT_VALUE_CHANGED, &p);
                }
                label(p.list, "Kalendername & Farbe", 0, &hp_font_de_24);
                p.appearanceCalendar = lv_dropdown_create(p.list);
                lv_obj_set_width(p.appearanceCalendar, 400);
                p.appearanceName = input(p, p.list, "Kalendername", 39);
                p.calendarColor = lv_dropdown_create(p.list);
                lv_dropdown_set_options(
                    p.calendarColor, "Blau\nRot\nGrün\nViolett\nOrange\nTürkis\nRosa\nGelb\nStahlbl"
                                     "au\nLimette\nBeere\nIndigo\nSand\nMint\nKoralle\nLavendel");
                lv_obj_add_event_cb(
                    p.appearanceCalendar,
                    [](lv_event_t *e) {
                        auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                        unsigned index = lv_dropdown_get_selected(p.appearanceCalendar);
                        if (index >= p.controller.calendars.size())
                            return;
                        const auto &c = p.controller.calendars[index];
                        lv_textarea_set_text(p.appearanceName, c.name);
                        lv_dropdown_set_selected(p.calendarColor, c.colorIndex);
                    },
                    LV_EVENT_VALUE_CHANGED, &p);
                push(
                    p.list, "Darstellung speichern",
                    [](lv_event_t *e) {
                        auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                        unsigned index = lv_dropdown_get_selected(p.appearanceCalendar);
                        if (index >= p.controller.calendars.size())
                            return;
                        JsonDocument args;
                        args["calendar_id"] = p.controller.calendars[index].id;
                        args["name"] = lv_textarea_get_text(p.appearanceName);
                        args["color"] = lv_dropdown_get_selected(p.calendarColor);
                        p.controller.action("appearance", args);
                    },
                    &p, 210);
                push(p.list,"Benutzer verwalten",[](lv_event_t*){UiShell::instance().requestAppOpen("settings","{\"view\":\"users\"}");},&p,240);
                label(p.list, "Lokalen Kalender anlegen", 0, &hp_font_de_24);
                p.calendarName = input(p, p.list, "Kalendername", 39);
                p.calendarKind = lv_dropdown_create(p.list);
                lv_dropdown_set_options(p.calendarKind, "Privat\nFamilie\nHaushalt");
                p.calendarOwner = lv_dropdown_create(p.list);
                lv_obj_set_width(p.calendarOwner, 400);
                push(
                    p.list, "Kalender erstellen",
                    [](lv_event_t *event) {
                        auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(event));
                        JsonDocument args;
                        args["name"] = lv_textarea_get_text(p.calendarName);
                        args["kind"] = lv_dropdown_get_selected(p.calendarKind);
                        const unsigned owner = lv_dropdown_get_selected(p.calendarOwner);
                        if ((args["kind"] | 0u) == 0 && !owner) {
                            lv_label_set_text(p.status,
                                              "Für einen privaten Kalender eine Person auswählen");
                            return;
                        }
                        args["owner"] = owner && owner <= p.controller.people.size()
                                            ? p.controller.people[owner - 1].id
                                            : 0;
                        p.controller.action("calendar_create", args);
                    },
                    &p, 190);
                label(p.list, "Feiertage Deutschland", 0, &hp_font_de_24);
                p.regionState = lv_dropdown_create(p.list);
                lv_dropdown_set_options(
                    p.regionState,
                    "BW\nBY\nBE\nBB\nHB\nHH\nHE\nMV\nNI\nNW\nRP\nSL\nSN\nST\nSH\nTH");
                const char *captions[] = {"Mariä Himmelfahrt (Gemeinde)", "Stadt Augsburg",
                                          "Fronleichnam (kommunaler Sonderfall)"};
                for (unsigned i = 0; i < 3; ++i) {
                    p.municipal[i] = lv_checkbox_create(p.list);
                    lv_checkbox_set_text(p.municipal[i], captions[i]);
                    readableText(p.municipal[i], &hp_font_de_18);
                }
                push(
                    p.list, "Region speichern",
                    [](lv_event_t *event) {
                        auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(event));
                        JsonDocument args;
                        char state[4];
                        lv_dropdown_get_selected_str(p.regionState, state, sizeof(state));
                        args["state"] = state;
                        args["assumption"] =
                            bool(lv_obj_has_state(p.municipal[0], LV_STATE_CHECKED));
                        args["augsburg"] = bool(lv_obj_has_state(p.municipal[1], LV_STATE_CHECKED));
                        args["corpus_christi_municipality"] =
                            bool(lv_obj_has_state(p.municipal[2], LV_STATE_CHECKED));
                        p.controller.action("region", args);
                    },
                    &p, 180);
                lv_dropdown_set_selected(p.regionState, 1);
                label(p.list, "Synchronisierung", 0, &hp_font_de_24);
                p.accountStatus = label(p.list, "Wird geladen", 0, &hp_font_de_18);
                label(p.list,
                      "Personenwahl ist ein Filter, keine Anmeldung. Private Änderungen benötigen "
                      "den nativen Einstellungs-PIN.",
                      0, &hp_font_de_14);
            }
            for (unsigned i = 0; i < 32; ++i) {
                if (i >= c.calendars.size()) {
                    lv_obj_add_flag(p.visibility[i], LV_OBJ_FLAG_HIDDEN);
                    continue;
                }
                const auto &cal = c.calendars[i];
                lv_checkbox_set_text(p.visibility[i], cal.name);
                lv_obj_set_style_text_color(p.visibility[i], lv_color_hex(c.color(cal.id)), 0);
                lv_obj_remove_flag(p.visibility[i], LV_OBJ_FLAG_HIDDEN);
                if (cal.enabled)
                    lv_obj_add_state(p.visibility[i], LV_STATE_CHECKED);
                else
                    lv_obj_remove_state(p.visibility[i], LV_STATE_CHECKED);
            }
            String names="Gemeinsam / kein Benutzer";
            for(const auto& person:c.people)names+="\n"+String(person.displayName);
            if(names!=lv_dropdown_get_options(p.calendarOwner))lv_dropdown_set_options(p.calendarOwner,names.c_str());
            String calendarNames;
            for(unsigned i=0;i<2;++i){if(i>=c.features.size()){lv_obj_add_flag(p.sharedFeature[i],LV_OBJ_FLAG_HIDDEN);continue;}lv_obj_remove_flag(p.sharedFeature[i],LV_OBJ_FLAG_HIDDEN);lv_checkbox_set_text(p.sharedFeature[i],c.features[i].name);if(c.features[i].enabled)lv_obj_add_state(p.sharedFeature[i],LV_STATE_CHECKED);else lv_obj_remove_state(p.sharedFeature[i],LV_STATE_CHECKED);}
            for (const auto &cal : c.calendars) {
                if (calendarNames.length())
                    calendarNames += '\n';
                calendarNames += cal.name;
            }
            if (calendarNames != lv_dropdown_get_options(p.appearanceCalendar)) {
                lv_dropdown_set_options(p.appearanceCalendar, calendarNames.c_str());
                lv_textarea_set_text(p.appearanceName, c.calendars.front().name);
                lv_dropdown_set_selected(p.calendarColor, c.calendars.front().colorIndex);
            }
            if (!p.regionLoaded && !c.region["state"].isNull()) {
                p.regionLoaded = true;
                static const char *states[] = {"BW", "BY", "BE", "BB", "HB", "HH", "HE", "MV",
                                               "NI", "NW", "RP", "SL", "SN", "ST", "SH", "TH"};
                for (unsigned i = 0; i < 16; ++i)
                    if (!strcmp(states[i], c.region["state"] | "BY"))
                        lv_dropdown_set_selected(p.regionState, i);
                static const char *keys[] = {"assumption", "augsburg",
                                             "corpus_christi_municipality"};
                for (unsigned i = 0; i < 3; ++i)
                    if (c.region[keys[i]] | false)
                        lv_obj_add_state(p.municipal[i], LV_STATE_CHECKED);
                    else
                        lv_obj_remove_state(p.municipal[i], LV_STATE_CHECKED);
            }
            String accounts = (c.status["storage_healthy"] | false)
                                  ? "Lokaler Speicher bereit"
                                  : "Lokaler Speicher nicht verfügbar";
            for (JsonObject account : c.status["accounts"].as<JsonArray>()) {
                accounts +=
                    "\n" + String(account["email"] | "") + ": " + String(account["message"] | "");
                if (account["truncated"] | false)
                    accounts += " (begrenzter Cache)";
            }
            lv_label_set_text(p.accountStatus, accounts.c_str());
            if (!p.collectionAccount) {
                label(p.list, "Online-Kalender auswählen", 0, &hp_font_de_24);
                label(p.list,
                      "Online-Kalender: Auswahl bestimmt die Synchronisierung. Sichtbarkeit oben "
                      "ändert nur die Anzeige.",
                      0, &hp_font_de_14);
                p.collectionAccount = lv_dropdown_create(p.list);
                lv_obj_set_width(p.collectionAccount, 440);
                label(p.list,"Kalenderkonto einem Benutzer zuordnen",0,&hp_font_de_18);
                p.accountPerson = lv_dropdown_create(p.list);
                lv_obj_set_width(p.accountPerson,440);
                label(p.list,"Benutzer über die gemeinsame Benutzerverwaltung anlegen. Mehrere Konten können demselben Benutzer gehören. Zugangsdaten bleiben in der zentralen Kontenverwaltung.",0,&hp_font_de_14);
                push(p.list,"Benutzerzuordnung speichern",[](lv_event_t *e){
                    auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));
                    const unsigned account=lv_dropdown_get_selected(p.collectionAccount),person=lv_dropdown_get_selected(p.accountPerson);
                    if(account>=p.collectionAccountCount||person>p.controller.people.size()){lv_label_set_text(p.status,"Kalenderkonto und Benutzer auswählen");return;}
                    JsonDocument args;args["slot"]=p.collectionSlots[account];args["person_id"]=person?p.controller.people[person-1].id:0;
                    if(p.controller.action("account_person",args))p.collectionsDirty=true;
                },&p,300);
                lv_obj_add_event_cb(
                    p.collectionAccount,
                    [](lv_event_t *e) {
                        auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                        p.collectionsDirty = true;
                        p.controller.dirty = true;
                    },
                    LV_EVENT_VALUE_CHANGED, &p);
                for (auto &check : p.collectionRows) {
                    check = lv_checkbox_create(p.list);
                    readableText(check, &hp_font_de_18);
                    lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
                }
                push(
                    p.list, "Kalenderauswahl speichern",
                    [](lv_event_t *e) {
                        auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                        const unsigned selected = lv_dropdown_get_selected(p.collectionAccount);
                        if (selected >= p.collectionAccountCount)
                            return;
                        const auto slot = p.collectionSlots[selected];
                        JsonDocument args;
                        args["slot"] = slot;
                        auto items = args["items"].to<JsonArray>();
                        for (JsonObject account : p.controller.status["accounts"].as<JsonArray>())
                            if ((account["slot"] | 255) == slot) {
                                unsigned i = 0;
                                for (JsonObject collection :
                                     account["collections"].as<JsonArray>()) {
                                    if (i >= 16)
                                        break;
                                    if (lv_obj_has_state(p.collectionRows[i++], LV_STATE_CHECKED))
                                        items.add(collection["id"]);
                                }
                            }
                        p.controller.action("collection_selection", args);
                    },
                    &p, 270);
            }
            String choices;
            unsigned accountCount = 0;
            for (JsonObject account : c.status["accounts"].as<JsonArray>())
                if ((account["collection_selection_supported"] | false) && accountCount < 5) {
                    p.collectionSlots[accountCount++] = account["slot"] | uint8_t(255);
                    if (choices.length())
                        choices += '\n';
                    choices += String(account["email"] | "");
                }
            p.collectionAccountCount = accountCount;
            if(p.shownStatusGeneration!=c.statusGeneration){p.shownStatusGeneration=c.statusGeneration;p.collectionsDirty=true;}
            String userChoices="Gemeinsam / kein Benutzer";
            for(const auto& person:c.people)userChoices+="\n"+String(person.displayName);
            if(userChoices!=lv_dropdown_get_options(p.accountPerson)){lv_dropdown_set_options(p.accountPerson,userChoices.c_str());p.collectionsDirty=true;}
            if (!choices.length())
                choices = "Keine Kalenderkonten eingerichtet";
            if (choices != lv_dropdown_get_options(p.collectionAccount)) {
                lv_dropdown_set_options(p.collectionAccount, choices.c_str());
                p.collectionsDirty = true;
            }
            if (p.collectionsDirty) {
                p.collectionsDirty = false;
                for (auto *check : p.collectionRows)
                    lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
                const unsigned index = lv_dropdown_get_selected(p.collectionAccount);
                if (index < accountCount)
                    for (JsonObject account : c.status["accounts"].as<JsonArray>())
                        if ((account["slot"] | 255) == p.collectionSlots[index]) {
                            const char* owner=account["person_id"]|"00000000";
                            const uint32_t ownerId=strtoul(owner,nullptr,16);unsigned selectedPerson=0;
                            for(unsigned j=0;j<c.people.size();++j)if(c.people[j].id==ownerId)selectedPerson=j+1;
                            lv_dropdown_set_selected(p.accountPerson,selectedPerson);
                            unsigned i = 0;
                            for (JsonObject collection : account["collections"].as<JsonArray>()) {
                                if (i >= 16)
                                    break;
                                auto *check = p.collectionRows[i++];
                                lv_checkbox_set_text(check, collection["name"] | "Kalender");
                                lv_obj_remove_flag(check, LV_OBJ_FLAG_HIDDEN);
                                if (collection["selected"] | false)
                                    lv_obj_add_state(check, LV_STATE_CHECKED);
                                else
                                    lv_obj_remove_state(check, LV_STATE_CHECKED);
                            }
                        }
            }
        }
    } // namespace
    void CalendarApp::create(lv_obj_t *parent, bool showSettings) {
        close();
        if (!parent) return;
        // The ESP32-P4 can have ample PSRAM but too little contiguous internal
        // heap for LVGL's first paint. Refuse unsafe startup with a visible
        // explanation rather than cascading through partly created widgets.
        const size_t largestInternal =
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (largestInternal < 12 * 1024) {
            auto *warning = lv_label_create(parent);
            if (warning)
                lv_label_set_text(warning,
                    "Kalender: zu wenig interner RAM. Bitte andere Apps schliessen.");
            return;
        }
        page_ = new (std::nothrow) NativeCalendarPage;
        if (!page_) {
            auto *warning = lv_label_create(parent);
            if (warning)
                lv_label_set_text(warning, "Kalender: zu wenig freier Speicher.");
            return;
        }
        auto &p = *page_;
        p.usersOnly=!showSettings&&usersRequested_;
        if(p.usersOnly)p.controller.setOwner("@users-native");
        showSettings=showSettings||p.usersOnly;
        p.settings = showSettings;
        p.root = lv_obj_create(parent);
        if (!p.root) {
            delete page_;
            page_ = nullptr;
            return;
        }
        UiTheme::instance().styleScreen(p.root);
        lv_obj_set_size(p.root, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(p.root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(p.root, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(p.root, 14, 0);
        lv_obj_set_style_pad_row(p.root, 10, 0);
        lv_obj_add_event_cb(p.root,outsideKeyboardTap,LV_EVENT_PRESSED,&p);
        lv_obj_add_event_cb(
            p.root,
            [](lv_event_t *event) {
                auto *app = static_cast<CalendarApp *>(lv_event_get_user_data(event));
                if (!app->page_ || lv_event_get_target(event) != app->page_->root)
                    return;
                auto *page = app->page_;
                app->page_ = nullptr;
                detachPageCallbacks(page->root, page);
                if (page->keyboard && lv_obj_is_valid(page->keyboard))
                    lv_keyboard_set_textarea(page->keyboard, nullptr);
                if (page->timer)
                    lv_timer_delete(page->timer);
                page->controller.close();
                delete page;
            },
            LV_EVENT_DELETE, this);
        auto *toolbar = row(p.root, 48);
        p.heading = label(toolbar, p.usersOnly?"Benutzer · Kalender & Telefonbuch":"Kalender", 0, &hp_font_de_28);
        lv_obj_set_flex_grow(p.heading, 1);
        if (!showSettings) {
            push(
                toolbar, "<",
                [](lv_event_t *e) {
                    auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                    p.listOffset = 0;
                    p.controller.select(
                        calendar::moveSelection(p.controller.selected, p.controller.mode, -1),
                        p.controller.mode);
                    render(p);
                },
                &p, 42);
            push(
                toolbar, "Heute",
                [](lv_event_t *e) {
                    auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                    p.controller.select(calendar::localDate(time(nullptr)), p.controller.mode);
                    render(p);
                },
                &p, 80);
            push(
                toolbar, ">",
                [](lv_event_t *e) {
                    auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                    p.listOffset = 0;
                    p.controller.select(
                        calendar::moveSelection(p.controller.selected, p.controller.mode, 1),
                        p.controller.mode);
                    render(p);
                },
                &p, 42);
            const char *modes[] = {"Monat", "Woche", "Tag", "Liste"};
            for (unsigned i = 0; i < 4; ++i) {
                auto *b = push(
                    toolbar, modes[i],
                    [](lv_event_t *e) {
                        auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                        const unsigned mode =
                            lv_obj_get_index(static_cast<lv_obj_t *>(lv_event_get_target(e))) - 4;
                        p.listOffset = 0;
                        p.controller.select(p.controller.selected, calendar::ViewMode(mode));
                        render(p);
                    },
                    &p, 70);
                (void)b;
            }
            push(
                toolbar, "+ Termin",
                [](lv_event_t *e) {
                    auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                    if (!p.controller.busy)
                        editor(p, true);
                },
                &p, 110);
            push(
                toolbar, LV_SYMBOL_SETTINGS,
                [](lv_event_t *) { UiShell::instance().requestAppSettings("calendar"); }, &p, 44);
            push(toolbar,"Benutzer",[](lv_event_t*){UiShell::instance().requestAppOpen("settings","{\"view\":\"users\"}");},&p,120);
        }
        p.status = label(p.root, "Lokale Daten werden geladen", 0, &hp_font_de_14);
        if(p.usersOnly){
            push(toolbar,"Telefonbuch",[](lv_event_t* e){chooseProfile(*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e)),0,"phonebook");},&p,150);
            push(toolbar,"Kalender",[](lv_event_t* e){chooseProfile(*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e)),0,"calendar");},&p,140);
            push(toolbar,"PIN freigeben",[](lv_event_t*){SecurityUi::requestSettingsUnlock();},&p,150);
            auto* body=row(p.root,0);lv_obj_set_flex_grow(body,1);
            auto* side=content(body,true);lv_obj_set_width(side,LV_PCT(28));lv_obj_set_style_min_width(side,180,0);lv_obj_set_style_max_width(side,260,0);lv_obj_set_height(side,LV_PCT(100));
            label(side,"Benutzer",0,&hp_font_de_24);
            p.userSearch=input(p,side,"Benutzer suchen",64);
            push(side,"+ Benutzer",[](lv_event_t* e){auto& p=*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e));if(!p.personChoice||p.controller.busy)return;chooseProfile(p,0);},&p,LV_PCT(100));
            p.userList=content(side,true);lv_obj_set_width(p.userList,LV_PCT(100));
            lv_obj_add_event_cb(p.userSearch,[](lv_event_t* e){renderUsersList(*static_cast<NativeCalendarPage*>(lv_event_get_user_data(e)));},LV_EVENT_VALUE_CHANGED,&p);
            p.list=content(body,true);lv_obj_set_height(p.list,LV_PCT(100));lv_obj_set_flex_grow(p.list,1);
        }
        else if (showSettings)
            settings(p);
        else {
            auto *filtersRow = row(p.root, 42);
            p.preset = lv_dropdown_create(filtersRow);
            lv_dropdown_set_options(p.preset, "Alle\nFamilie\nPrivat");
            lv_obj_set_width(p.preset, 130);
            p.person = lv_dropdown_create(filtersRow);
            lv_obj_set_width(p.person, 200);
            p.calendar = lv_dropdown_create(filtersRow);
            lv_obj_set_width(p.calendar, 270);
            for (auto *obj : {p.preset, p.person, p.calendar}) {
                darkInput(obj);
                lv_obj_add_event_cb(obj, filters, LV_EVENT_VALUE_CHANGED, &p);
            }
            push(
                filtersRow, "Aktualisieren",
                [](lv_event_t *e) {
                    auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                    p.controller.refresh(true);
                },
                &p, 140);
            auto *body = row(p.root, 0);
            lv_obj_set_flex_grow(body, 1);
            lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_START);
            p.scroll = content(body, true);
            lv_obj_set_height(p.scroll, LV_PCT(100));
            lv_obj_set_flex_grow(p.scroll, 1);
            lv_obj_set_style_pad_all(p.scroll, 0, 0);
            p.board = lv_obj_create(p.scroll);
            lv_obj_set_size(p.board, LV_PCT(100), LV_PCT(100));
            lv_obj_set_flex_grow(p.board, 0);
            transparent(p.board);
            lv_obj_clear_flag(p.board, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(p.board, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(p.board, draw, LV_EVENT_DRAW_MAIN_END, &p);
            lv_obj_add_event_cb(p.board, touched, LV_EVENT_CLICKED, &p);
            auto *side = content(body, false);
            p.side = side;
            lv_obj_set_size(side, 330, LV_PCT(100));
            lv_obj_set_flex_grow(side, 0);
            p.search = input(p, side, "Termine / Suche", 71);
            auto *pages = row(side, 40);
            p.listPrevious = push(
                pages, "Vorige",
                [](lv_event_t *e) {
                    auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                    p.listOffset = p.listOffset >= 12 ? p.listOffset - 12 : 0;
                    renderList(p);
                },
                &p);
            p.listNext = push(
                pages, "Weitere",
                [](lv_event_t *e) {
                    auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                    p.listOffset += 12;
                    renderList(p);
                },
                &p);
            p.list = content(side, true);
            for (unsigned i = 0; i < 12; ++i) {
                auto *b = lv_button_create(p.list);
                p.rows[i] = b;
                UiTheme::instance().styleCard(b);
                lv_obj_set_size(b, LV_PCT(100), 68);
                lv_obj_set_style_border_width(b, 2, 0);
                lv_obj_set_style_shadow_width(b, 0, 0);
                label(b, "", LV_PCT(100), &hp_font_de_14);
                lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_event_cb(
                    b,
                    [](lv_event_t *e) {
                        auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                        for (unsigned i = 0; i < 12; ++i)
                            if (p.rows[i] == lv_event_get_target(e)) {
                                openItem(p, p.rowItems[i]);
                                return;
                            }
                    },
                    LV_EVENT_CLICKED, &p);
            }
            lv_obj_add_event_cb(
                p.search,
                [](lv_event_t *e) {
                    auto &p = *static_cast<NativeCalendarPage *>(lv_event_get_user_data(e));
                    p.listOffset = 0;
                    renderList(p);
                },
                LV_EVENT_VALUE_CHANGED, &p);
        }
        p.controller.open(p.usersOnly);
        p.unlocked = SecurityService::instance().settingsAllowed();
        // The UI has been built synchronously; backend jobs may poll after
        // the initial paint. 160 ms is enough for interaction without churn.
        p.timer = lv_timer_create(poll, 160, &p);
        render(p);
    }
    void CalendarApp::open(lv_obj_t *parent) { create(parent, false); }
    void CalendarApp::setLaunchArguments(const String& arguments){JsonDocument doc;const bool users=!deserializeJson(doc,arguments)&&doc["view"]=="users";usersRequested_=users;if(page_&&page_->usersOnly!=users){auto* parent=lv_obj_get_parent(page_->root);create(parent,false);}}
    bool CalendarApp::openSettings(lv_obj_t *parent) {
        usersRequested_=false;
        create(parent, true);
        return page_ != nullptr;
    }
    void CalendarApp::close() {
        if (page_ && page_->root && lv_obj_is_valid(page_->root))
            lv_obj_delete(page_->root);
    }
} // namespace hp

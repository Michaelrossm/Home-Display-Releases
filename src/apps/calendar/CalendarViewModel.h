#pragma once
#include "MonthGridModel.h"
#include <algorithm>
#include <array>

namespace hp::calendar {
    enum class ViewMode : uint8_t { Month, Week, Day, Agenda };
    struct ViewRange {
        Date first{}, last{}; // exclusive last
        int64_t from = 0, to = 0;
    };
    inline ViewRange viewRange(Date selected, ViewMode mode) {
        if (!validDate(selected.year, selected.month, selected.day))
            return {};
        const auto day = dayNumber(selected.year, selected.month, selected.day);
        ViewRange result;
        if (mode == ViewMode::Month) {
            MonthGridModel model;
            model.set(selected.year, selected.month, false);
            result.first = model.cells.front().date;
            auto date = model.cells.back().date;
            result.last = dateFromDay(dayNumber(date.year, date.month, date.day) + 1);
        } else if (mode == ViewMode::Week) {
            const auto start = day - weekday(selected.year, selected.month, selected.day);
            result.first = dateFromDay(start);
            result.last = dateFromDay(start + 7);
        } else {
            result.first = selected;
            result.last = dateFromDay(day + (mode == ViewMode::Day ? 1 : 30));
        }
        result.from = localEpoch(result.first);
        result.to = localEpoch(result.last);
        return result;
    }
    inline Date moveSelection(Date selected, ViewMode mode, int direction) {
        if (mode == ViewMode::Month) {
            int index = selected.year * 12 + selected.month - 1 + direction;
            Date next{index / 12, index % 12 + 1, selected.day};
            next.day = std::min(next.day, daysInMonth(next.year, next.month));
            return validDate(next.year, next.month, next.day) ? next : selected;
        }
        auto next = dateFromDay(dayNumber(selected.year, selected.month, selected.day) +
                                direction * (mode == ViewMode::Week ? 7 : 1));
        return validDate(next.year, next.month, next.day) ? next : selected;
    }
    // One native grid object performs hit-testing; no button per calendar day.
    inline int monthCellAt(int x, int y, int width, int height, int header = 32) {
        if (width <= 0 || height <= header || x < 0 || x >= width || y < header || y >= height)
            return -1;
        const int column = std::min(6, x * 7 / width),
                  row = std::min(5, (y - header) * 6 / (height - header));
        return row * 7 + column;
    }
    struct TimeBlock {
        int start = 0, end = 0;
        unsigned source = 0, lane = 0, lanes = 1;
    };
    // Connected overlap groups share the same lane width. Input bounded by caller.
    template <size_t N> void layoutOverlaps(std::array<TimeBlock, N> &blocks, size_t count) {
        count = std::min(count, N);
        std::sort(blocks.begin(), blocks.begin() + count, [](const auto &a, const auto &b) {
            return a.start != b.start ? a.start < b.start : a.end < b.end;
        });
        for (size_t first = 0; first < count;) {
            size_t last = first + 1;
            int finish = blocks[first].end;
            while (last < count && blocks[last].start < finish) {
                finish = std::max(finish, blocks[last].end);
                ++last;
            }
            std::array<int, N> ends{};
            unsigned lanes = 0;
            for (size_t i = first; i < last; ++i) {
                unsigned lane = 0;
                while (lane < lanes && ends[lane] > blocks[i].start)
                    ++lane;
                if (lane == lanes)
                    ++lanes;
                ends[lane] = blocks[i].end;
                blocks[i].lane = lane;
            }
            for (size_t i = first; i < last; ++i)
                blocks[i].lanes = lanes;
            first = last;
        }
    }
} // namespace hp::calendar

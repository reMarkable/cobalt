// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A Cobalt day-index assigns to each date of the calendar on or after
// Junuary 1, 1970 a non-negative integer index with
// January 1, 1970 being assigned the index 0. For example
//
// Calendar Date            Cobalt day-index
// ----------              ----------------
// January 1, 1970          0
// January 2, 1970          1
// February 1, 1970         31
// October 18, 2016         17,092
//
// Notice that a day-index does not represent a fixed 24-hour period of real
// time because the day with day-index n has a different meaning in
// different time-zones. Thus there is no well-defined mapping from an
// instant of time to a day index.

#ifndef COBALT_UTIL_DATETIME_UTIL_H_
#define COBALT_UTIL_DATETIME_UTIL_H_

#include <cstdint>

namespace cobalt {
namespace util {

// Unix seconds differ from physical seconds on a day in which there is
// a leap second.
static const uint32_t kNumUnixSecondsPerDay = 60L * 60L * 24L;

// A CalendarDate represents a day on the calendar using normal human-readable
// indexing. Just as with day-index there is no well-defined mapping from an
// instant of time to a CalendarDate because this depends on the timezone.
struct CalendarDate {
  // A number from 1 to 31.
  uint32_t day_of_month;

  // 1 = January, 2 = February, ..., 3 = December
  uint32_t month;

  // Calendar year e.g. 2016.
  uint32_t year;

  bool operator==(const CalendarDate& other) const {
    return (other.day_of_month == day_of_month
        && other.month == month && other.year == year);
  }
};

// Converts the given CalendarDate to a Cobalt Day Index. If the fields of
// calendar_date do not make sense as a real day of the calendar
// (for example if month=13) then the result is undefined. If the
// specified date is prior to January 1, 1970 then the result is undefined.
uint32_t CalendarDateToDayIndex(const CalendarDate& calendar_date);

// Converts the given day_index to a CalendarDate, necessarily on or
// after January 1, 1970.
CalendarDate DayIndexToCalendarDate(uint32_t  day_index);


}  // namespace util
}  // namespace cobalt

#endif  // COBALT_UTIL_DATETIME_UTIL_H_

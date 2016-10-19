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

#include "util/datetime_util.h"

#include <ctime>

#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace util {

// This is an alternate definition of CalendarDateToDayIndex from the one
// in datetime_util.cc. It appears to be simpler than the one we chose. The
// reasons we did not choose this implementation are:
// (a) It uses the function timegm which is nonstandard and may not be supported
// on all platforms we want to support. See:
// http://man7.org/linux/man-pages/man3/timegm.3.html#CONFORMING_TO
// (b) In order to imlement timegm ourselves using standard library functions
// it is necessary to invoke functions that make use of the local timezone
// and that are not thread-safe because they use global state. Both of these
// things are unnecessary. It seemed like a better idea to use a pure
// algorithm that would work anywhere.
uint32_t CalendarDateToDayIndexAltImpl(const CalendarDate& calendar_date) {
  struct tm time_info;
  time_info.tm_sec = 0;
  time_info.tm_min = 0;
  time_info.tm_hour = 0;
  time_info.tm_mday = calendar_date.day_of_month;
  time_info.tm_mon = calendar_date.month - 1;
  time_info.tm_year = calendar_date.year - 1900;
  time_t unix_time = timegm(&time_info);
  return unix_time/kNumUnixSecondsPerDay;
}

TEST(DatetimeUtilTest, CalendarDateToDayIndex) {
  CalendarDate calendar_date;

  // January 1, 1970
  calendar_date.day_of_month = 1;
  calendar_date.month = 1;
  calendar_date.year = 1970;
  EXPECT_EQ(0, CalendarDateToDayIndex(calendar_date));
  EXPECT_EQ(0, CalendarDateToDayIndexAltImpl(calendar_date));

  // January 2, 1970
  calendar_date.day_of_month = 2;
  EXPECT_EQ(1, CalendarDateToDayIndex(calendar_date));
  EXPECT_EQ(1, CalendarDateToDayIndexAltImpl(calendar_date));

  // January 31, 1970
  calendar_date.day_of_month = 31;
  EXPECT_EQ(30, CalendarDateToDayIndex(calendar_date));
  EXPECT_EQ(30, CalendarDateToDayIndexAltImpl(calendar_date));

  // Febrary 1, 1970
  calendar_date.month = 2;
  calendar_date.day_of_month = 1;
  EXPECT_EQ(31, CalendarDateToDayIndex(calendar_date));
  EXPECT_EQ(31, CalendarDateToDayIndexAltImpl(calendar_date));

  // 1972 Was a leap year...

  // January 1, 1972
  calendar_date.day_of_month = 1;
  calendar_date.month = 1;
  calendar_date.year = 1972;
  EXPECT_EQ(365*2, CalendarDateToDayIndex(calendar_date));
  EXPECT_EQ(365*2, CalendarDateToDayIndexAltImpl(calendar_date));

  // February 1, 1972
  calendar_date.day_of_month = 1;
  calendar_date.month = 2;
  calendar_date.year = 1972;
  EXPECT_EQ(365*2 + 31, CalendarDateToDayIndex(calendar_date));
  EXPECT_EQ(365*2 + 31, CalendarDateToDayIndexAltImpl(calendar_date));

  // February 28, 1972
  calendar_date.day_of_month = 28;
  calendar_date.month = 2;
  calendar_date.year = 1972;
  EXPECT_EQ(365*2 + 31 + 27, CalendarDateToDayIndex(calendar_date));
  EXPECT_EQ(365*2 + 31 + 27, CalendarDateToDayIndexAltImpl(calendar_date));

  // February 29, 1972
  calendar_date.day_of_month = 29;
  calendar_date.month = 2;
  calendar_date.year = 1972;
  EXPECT_EQ(365*2 + 31 + 28, CalendarDateToDayIndex(calendar_date));
  EXPECT_EQ(365*2 + 31 + 28, CalendarDateToDayIndexAltImpl(calendar_date));

  // March 1, 1972
  calendar_date.day_of_month = 1;
  calendar_date.month = 3;
  calendar_date.year = 1972;
  EXPECT_EQ(365*2 + 31 + 29, CalendarDateToDayIndex(calendar_date));
  EXPECT_EQ(365*2 + 31 + 29, CalendarDateToDayIndexAltImpl(calendar_date));

  // January 1, 1973
  calendar_date.day_of_month = 1;
  calendar_date.month = 1;
  calendar_date.year = 1973;
  EXPECT_EQ(365*2 + 366, CalendarDateToDayIndex(calendar_date));
  EXPECT_EQ(365*2 + 366, CalendarDateToDayIndexAltImpl(calendar_date));

  // March 1, 2000 (2000 years after March 1, year 0)
  calendar_date.day_of_month = 1;
  calendar_date.month = 3;
  calendar_date.year = 2000;
  // See comments at KEpochOffset.
  EXPECT_EQ(11017, CalendarDateToDayIndex(calendar_date));
  EXPECT_EQ(11017, CalendarDateToDayIndexAltImpl(calendar_date));

  // October 18, 2016
  calendar_date.day_of_month = 18;
  calendar_date.month = 10;
  calendar_date.year = 2016;
  EXPECT_EQ(17092, CalendarDateToDayIndex(calendar_date));
  EXPECT_EQ(17092, CalendarDateToDayIndexAltImpl(calendar_date));
}

void doDayIndexToCalendarDateTest(uint32_t day_index, uint32_t expected_month,
    uint32_t expected_day_of_month, uint32_t expected_year) {
  auto calendar_date = DayIndexToCalendarDate(day_index);
  EXPECT_EQ(expected_day_of_month, calendar_date.day_of_month)
      << "day_index=" << day_index;
  EXPECT_EQ(expected_month, calendar_date.month)
      << "day_index=" << day_index;
  EXPECT_EQ(expected_year, calendar_date.year)
      << "day_index=" << day_index;
}

TEST(DatetimeUtilTest, DayIndexToCalendarDate) {
  // January 1, 1970
  doDayIndexToCalendarDateTest(0, 1, 1, 1970);

  // January 2, 1970
  doDayIndexToCalendarDateTest(1, 1, 2, 1970);

  // January 31, 1970
  doDayIndexToCalendarDateTest(30, 1, 31, 1970);

  // Febrary 1, 1970
  doDayIndexToCalendarDateTest(31, 2, 1, 1970);

  // 1972 Was a leap year...

  // January 1, 1972
  doDayIndexToCalendarDateTest(365*2, 1, 1, 1972);

  // February 1, 1972
  doDayIndexToCalendarDateTest(365*2 + 31, 2, 1, 1972);

  // February 28, 1972
  doDayIndexToCalendarDateTest(365*2 + 31 + 27, 2, 28, 1972);

  // February 29, 1972
  doDayIndexToCalendarDateTest(365*2 + 31 + 28, 2, 29, 1972);

  // March 1, 1972
  doDayIndexToCalendarDateTest(365*2 + 31 + 29, 3, 1, 1972);

  // January 1, 1973
  doDayIndexToCalendarDateTest(365*2 + 366, 1, 1, 1973);

  // March 1, 2000
  doDayIndexToCalendarDateTest(11017, 3, 1, 2000);

  // October 18, 2016
  doDayIndexToCalendarDateTest(17092, 10, 18, 2016);
}

TEST(DatetimeUtilTest, DayIndexCalendarDateInversesTest) {
  for (uint32_t day_index = 16000; day_index < 19000; day_index++) {
    CalendarDate calendar_date = DayIndexToCalendarDate(day_index);
    EXPECT_EQ(day_index, CalendarDateToDayIndex(calendar_date));
    EXPECT_EQ(day_index, CalendarDateToDayIndexAltImpl(calendar_date));
  }
}


}  // namespace util
}  // namespace cobalt


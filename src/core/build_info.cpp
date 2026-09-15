#include "build_info.h"

#include <cstdio>
#include <cstring>

namespace bd::build_info {
namespace {

// Days since 1970-01-01 for a Gregorian date (Howard Hinnant's algorithm).
std::int64_t DaysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned dayOfEra =
        yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(dayOfEra) - 719468;
}

} // namespace

const char* Stamp()
{
    return __DATE__ " " __TIME__;
}

std::int64_t UtcTime()
{
    static const std::int64_t value = [] {
        char month[4]{};
        int day = 0;
        int year = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;

        std::sscanf(__DATE__, "%3s %d %d", month, &day, &year);
        std::sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

        static const char* names[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        unsigned index = 0;
        for (unsigned i = 0; i < 12; ++i) {
            if (std::strcmp(names[i], month) == 0) {
                index = i + 1;
                break;
            }
        }

        return DaysFromCivil(year, index, static_cast<unsigned>(day)) * 86400 + hour * 3600 +
               minute * 60 + second;
    }();
    return value;
}

std::int64_t EpochFromIso(const std::string& iso)
{
    int year = 0;
    unsigned month = 1;
    unsigned day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (std::sscanf(iso.c_str(), "%d-%u-%uT%d:%d:%d", &year, &month, &day, &hour, &minute,
                    &second) < 3)
        return 0;

    return DaysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}

} // namespace bd::build_info

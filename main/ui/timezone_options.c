#include "timezone_options.h"

#include <string.h>

typedef struct {
    const char *posix;
    const char *label;
} timezone_entry_t;

/* POSIX TZ strings for setenv("TZ", ...). Offsets follow the POSIX convention. */
static const timezone_entry_t s_timezones[] = {
    {"UTC0", "UTC0 - UTC / GMT"},
    {"GMT0", "GMT0 - GMT"},
    {"EST5EDT", "EST5EDT - US Eastern"},
    {"CST6CDT", "CST6CDT - US Central"},
    {"MST7MDT", "MST7MDT - US Mountain"},
    {"MST7", "MST7 - Arizona (no DST)"},
    {"PST8PDT", "PST8PDT - US Pacific"},
    {"AKST9AKDT", "AKST9AKDT - Alaska"},
    {"HST10", "HST10 - Hawaii"},
    {"AST4ADT", "AST4ADT - Atlantic (Canada)"},
    {"NST3:30NDT", "NST3:30NDT - Newfoundland"},
    {"WET0WEST", "WET0WEST - UK / Ireland / Portugal"},
    {"CET-1CEST", "CET-1CEST - Central Europe"},
    {"EET-2EEST", "EET-2EEST - Eastern Europe"},
    {"MSK-3", "MSK-3 - Moscow"},
    {"IST-5:30", "IST-5:30 - India"},
    {"CST-8", "CST-8 - China"},
    {"JST-9", "JST-9 - Japan"},
    {"KST-9", "KST-9 - Korea"},
    {"AEST-10AEDT", "AEST-10AEDT - Australia Eastern"},
    {"AWST-8", "AWST-8 - Australia Western"},
    {"NZST-12NZDT", "NZST-12NZDT - New Zealand"},
    {"BRT3", "BRT3 - Brazil (Brasilia)"},
    {"ART3", "ART3 - Argentina"},
};

static const char s_dropdown_options[] =
    "UTC0 - UTC / GMT\n"
    "GMT0 - GMT\n"
    "EST5EDT - US Eastern\n"
    "CST6CDT - US Central\n"
    "MST7MDT - US Mountain\n"
    "MST7 - Arizona (no DST)\n"
    "PST8PDT - US Pacific\n"
    "AKST9AKDT - Alaska\n"
    "HST10 - Hawaii\n"
    "AST4ADT - Atlantic (Canada)\n"
    "NST3:30NDT - Newfoundland\n"
    "WET0WEST - UK / Ireland / Portugal\n"
    "CET-1CEST - Central Europe\n"
    "EET-2EEST - Eastern Europe\n"
    "MSK-3 - Moscow\n"
    "IST-5:30 - India\n"
    "CST-8 - China\n"
    "JST-9 - Japan\n"
    "KST-9 - Korea\n"
    "AEST-10AEDT - Australia Eastern\n"
    "AWST-8 - Australia Western\n"
    "NZST-12NZDT - New Zealand\n"
    "BRT3 - Brazil (Brasilia)\n"
    "ART3 - Argentina";

size_t timezone_options_count(void)
{
    return sizeof(s_timezones) / sizeof(s_timezones[0]);
}

const char *timezone_options_posix(size_t index)
{
    if (index >= timezone_options_count()) {
        return "UTC0";
    }
    return s_timezones[index].posix;
}

const char *timezone_options_label(size_t index)
{
    if (index >= timezone_options_count()) {
        return s_timezones[0].label;
    }
    return s_timezones[index].label;
}

const char *timezone_options_dropdown_string(void)
{
    return s_dropdown_options;
}

int timezone_options_find_index(const char *posix)
{
    if (posix == NULL || posix[0] == '\0') {
        return 0;
    }

    for (size_t i = 0; i < timezone_options_count(); i++) {
        if (strcmp(posix, s_timezones[i].posix) == 0) {
            return (int)i;
        }
    }

    return -1;
}

// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>
#if defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif
#include "i3status.h"

#define BINARY_BASE 1024UL

static const char *const iec_symbols[] = {"B", "KiB", "MiB", "GiB", "TiB"};
#define MAX_EXPONENT ((sizeof iec_symbols / sizeof *iec_symbols) - 1)

/*
 * Prints the given amount of bytes in a human readable manner.
 *
 */
static int print_bytes_human(char *outwalk, unsigned long bytes, const char *unit, const int decimals) {
    double base = bytes;
    int exponent = 0;
    while (base >= BINARY_BASE && exponent < MAX_EXPONENT) {
        if (strcasecmp(unit, iec_symbols[exponent]) == 0) {
            break;
        }

        base /= BINARY_BASE;
        exponent += 1;
    }
    return sprintf(outwalk, "%.*f %s", decimals, base, iec_symbols[exponent]);
}

/*
 * Convert a string to its absolute representation based on the total
 * memory of `mem_total`.
 *
 * The string can contain any percentage values, which then return a
 * the value of `mem_amount` in relation to `mem_total`.
 * Alternatively an absolute value can be given, suffixed with an iec
 * symbol.
 *
 */
static unsigned long memory_absolute(const char *mem_amount, const unsigned long mem_total) {
    char *endptr;
    unsigned long amount = strtoul(mem_amount, &endptr, 10);

    while (endptr[0] != '\0' && isspace(endptr[0]))
        endptr++;

    switch (endptr[0]) {
        case 'T':
        case 't':
            amount *= BINARY_BASE;
        case 'G':
        case 'g':
            amount *= BINARY_BASE;
        case 'M':
        case 'm':
            amount *= BINARY_BASE;
        case 'K':
        case 'k':
            amount *= BINARY_BASE;
            break;
        case '%':
            amount = mem_total * amount / 100;
            break;
    }

    return amount;
}

void print_memory(yajl_gen json_gen, char *buffer, const char *format, const char *format_degraded, const char *threshold_degraded, const char *threshold_critical, const char *memory_used_method, const char *unit, const int decimals) {
    char *outwalk = buffer;

    const char *selected_format = format;
    const char *walk;
    const char *output_color = NULL;

    int unread_fields = 6;
    unsigned long ram_total;
    unsigned long ram_free;
    unsigned long ram_available;
    unsigned long ram_buffers;
    unsigned long ram_cached;
    unsigned long ram_shared;

#if defined(linux)
    FILE *file = fopen("/proc/meminfo", "r");
    if (!file) {
        goto error;
    }
    for (char line[128]; fgets(line, sizeof line, file);) {
        if (BEGINS_WITH(line, "MemTotal:")) {
            ram_total = strtoul(line + strlen("MemTotal:"), NULL, 10);
        } else if (BEGINS_WITH(line, "MemFree:")) {
            ram_free = strtoul(line + strlen("MemFree:"), NULL, 10);
        } else if (BEGINS_WITH(line, "MemAvailable:")) {
            ram_available = strtoul(line + strlen("MemAvailable:"), NULL, 10);
        } else if (BEGINS_WITH(line, "Buffers:")) {
            ram_buffers = strtoul(line + strlen("Buffers:"), NULL, 10);
        } else if (BEGINS_WITH(line, "Cached:")) {
            ram_cached = strtoul(line + strlen("Cached:"), NULL, 10);
        } else if (BEGINS_WITH(line, "Shmem:")) {
            ram_shared = strtoul(line + strlen("Shmem:"), NULL, 10);
        } else {
            continue;
        }
        if (--unread_fields == 0) {
            break;
        }
    }
    fclose(file);

    if (unread_fields > 0) {
        goto error;
    }

    // Values are in kB, convert them to B.
    ram_total *= 1024UL;
    ram_free *= 1024UL;
    ram_available *= 1024UL;
    ram_buffers *= 1024UL;
    ram_cached *= 1024UL;
    ram_shared *= 1024UL;
#elif defined(__FreeBSD__)
    size_t size;

    uint32_t page_size;
    size = sizeof(page_size);
    if (sysctlbyname("hw.pagesize", &page_size, &size, NULL, 0) != 0) {
        goto error;
    }

    unsigned long _ram_total;
    size = sizeof(_ram_total);
    if (sysctlbyname("hw.physmem", &_ram_total, &size, NULL, 0) == 0) {
        ram_total = (long)_ram_total;
    } else {
        goto error;
    }

    uint32_t _ram_free;
    size = sizeof(_ram_free);
    if (sysctlbyname("vm.stats.vm.v_free_count", &_ram_free, &size, NULL, 0) == 0) {
        ram_free = (long)_ram_free * (long)page_size;
    } else {
        goto error;
    }

    ram_available = 0;
    ram_buffers = 0;
    ram_cached = 0;
    ram_shared = 0;
#else
    OUTPUT_FULL_TEXT("");
    fputs("i3status: Memory status information is not supported on this system\n", stderr);
    return;
#endif

    unsigned long ram_used;
    if (BEGINS_WITH(memory_used_method, "memavailable")) {
        ram_used = ram_total - ram_available;
    } else if (BEGINS_WITH(memory_used_method, "classical")) {
        ram_used = ram_total - ram_free - ram_buffers - ram_cached;
    }

    if (threshold_degraded) {
        const unsigned long threshold = memory_absolute(threshold_degraded, ram_total);
        if (ram_available < threshold) {
            output_color = "color_degraded";
        }
    }

    if (threshold_critical) {
        const unsigned long threshold = memory_absolute(threshold_critical, ram_total);
        if (ram_available < threshold) {
            output_color = "color_bad";
        }
    }

    if (output_color) {
        START_COLOR(output_color);

        if (format_degraded)
            selected_format = format_degraded;
    }

    for (walk = selected_format; *walk != '\0'; walk++) {
        if (*walk != '%') {
            *(outwalk++) = *walk;

        } else if (BEGINS_WITH(walk + 1, "total")) {
            outwalk += print_bytes_human(outwalk, ram_total, unit, decimals);
            walk += strlen("total");

        } else if (BEGINS_WITH(walk + 1, "used")) {
            outwalk += print_bytes_human(outwalk, ram_used, unit, decimals);
            walk += strlen("used");

        } else if (BEGINS_WITH(walk + 1, "free")) {
            outwalk += print_bytes_human(outwalk, ram_free, unit, decimals);
            walk += strlen("free");

        } else if (BEGINS_WITH(walk + 1, "available")) {
            outwalk += print_bytes_human(outwalk, ram_available, unit, decimals);
            walk += strlen("available");

        } else if (BEGINS_WITH(walk + 1, "shared")) {
            outwalk += print_bytes_human(outwalk, ram_shared, unit, decimals);
            walk += strlen("shared");

        } else if (BEGINS_WITH(walk + 1, "percentage_free")) {
            outwalk += sprintf(outwalk, "%.01f%s", 100.0 * ram_free / ram_total, pct_mark);
            walk += strlen("percentage_free");

        } else if (BEGINS_WITH(walk + 1, "percentage_available")) {
            outwalk += sprintf(outwalk, "%.01f%s", 100.0 * ram_available / ram_total, pct_mark);
            walk += strlen("percentage_available");

        } else if (BEGINS_WITH(walk + 1, "percentage_used")) {
            outwalk += sprintf(outwalk, "%.01f%s", 100.0 * ram_used / ram_total, pct_mark);
            walk += strlen("percentage_used");

        } else if (BEGINS_WITH(walk + 1, "percentage_shared")) {
            outwalk += sprintf(outwalk, "%.01f%s", 100.0 * ram_shared / ram_total, pct_mark);
            walk += strlen("percentage_shared");

        } else {
            *(outwalk++) = '%';
        }
    }

    if (output_color)
        END_COLOR;

    *outwalk = '\0';
    OUTPUT_FULL_TEXT(buffer);

    return;
error:
    OUTPUT_FULL_TEXT("can't read memory");
    fputs("i3status: Cannot read system memory\n", stderr);
}

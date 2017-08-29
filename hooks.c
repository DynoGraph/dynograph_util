#include "hooks_c.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifdef __le64__
#include <memoryweb.h>
#endif

int64_t hooks_timestamp = 0;
const char* hooks_active_region = NULL;

void hooks_set_active_region(const char* name)
{
    hooks_active_region = name;
}

static bool region_is_active(const char* name)
{
    if (hooks_active_region == NULL) return true;
    else { return (bool)!strcmp(name, hooks_active_region); }
}

void hooks_region_begin(const char* name)
{

#ifdef __le64__
    hooks_timestamp = -CLOCK();
    if (region_is_active(name)) {
        starttiming();
    }
#endif
}
void hooks_region_end()
{
#ifdef __le64__
    hooks_timestamp += CLOCK();
    fprintf(stderr, "time_ticks = %llu\n", hooks_timestamp);
    stoptiming();
#endif
}
void hooks_set_attr_u64(const char * key, uint64_t value) { fprintf(stderr, "%s = %llu\n", key, value); }
void hooks_set_attr_i64(const char * key, int64_t value) { fprintf(stderr, "%s = %lli\n", key, value); }
void hooks_set_attr_f64(const char * key, double value) { fprintf(stderr, "%s = %f\n", key, value);}
void hooks_set_attr_str(const char * key, const char* value) {}
void hooks_traverse_edges(uint64_t n) {}

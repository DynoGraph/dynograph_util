#include "hooks_c.h"
#include <stdio.h>

#ifdef __le64__
#include <memoryweb.h>
#endif

void hooks_region_begin(const char* name)
{
#ifdef __le64__
starttiming();
#endif
}
void hooks_region_end()
{
#ifdef __le64__
stoptiming();
#endif
}
void hooks_set_attr_u64(const char * key, uint64_t value) {}
void hooks_set_attr_i64(const char * key, int64_t value) { fprintf(stderr, "%s = %lli\n", key, value); }
void hooks_set_attr_f64(const char * key, double value) {}
void hooks_set_attr_str(const char * key, const char* value) {}
void hooks_traverse_edges(uint64_t n) {}

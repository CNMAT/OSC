#pragma once
#include <stdlib.h>
extern int g_realloc, g_malloc, g_free, g_peak, g_live;
extern "C" void* counted_realloc(void*, size_t);
extern "C" void* counted_malloc(size_t);
extern "C" void  counted_free(void*);

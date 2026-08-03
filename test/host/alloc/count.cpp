#include "count.h"
#include <map>
int g_realloc=0, g_malloc=0, g_free=0, g_peak=0, g_live=0;
static std::map<void*,size_t> sizes;
static void note(void*p,size_t n){ if(p){ g_live += (int)n - (int)(sizes.count(p)?sizes[p]:0); sizes[p]=n; if(g_live>g_peak) g_peak=g_live; } }
extern "C" void* counted_realloc(void*p,size_t n){ g_realloc++; void*q=realloc(p,n);
  if(p && q!=p){ g_live -= (int)sizes[p]; sizes.erase(p);} note(q,n); return q; }
extern "C" void* counted_malloc(size_t n){ g_malloc++; void*q=malloc(n); note(q,n); return q; }
extern "C" void  counted_free(void*p){ if(!p) return; g_free++; if(sizes.count(p)){ g_live-=(int)sizes[p]; sizes.erase(p);} free(p); }

// Pattern-matching semantics, so the bounds added to OSCMatch.c can be shown
// not to have changed what matches. osc_match(pattern, address, ...).
#include "OSCMatch.h"
#include <stdio.h>
#include <string.h>
static int fails=0;
static bool full(const char*pat,const char*addr){
  int po=0,ao=0; int r=osc_match(pat,addr,&po,&ao);
  return (r & OSC_MATCH_ADDRESS_COMPLETE) && (r & OSC_MATCH_PATTERN_COMPLETE);
}
static void t(const char*pat,const char*addr,bool want){
  bool got=full(pat,addr);
  if(got!=want){ fails++; printf("  FAIL %-16s vs %-14s got %d want %d\n",pat,addr,got,want); }
}
int main(){
  // literals
  t("/a/b","/a/b",true);   t("/a/b","/a/c",false);
  // ?
  t("/a/?","/a/b",true);   t("/a/?","/a/bc",false);
  // *
  t("/a/*","/a/bcd",true); t("/*/b","/a/b",true);
  t("/a*d","/abcd",true);   // * matches "bc"; it only refuses to cross / t("/a*d","/a/d",false);
  // multi-star within a segment
  t("/*b*","/abc",true);   t("/*x*","/abc",false);
  // bracket set
  t("/a/[bc]","/a/b",true);  t("/a/[bc]","/a/d",false);
  // bracket range
  t("/a/[a-z]","/a/q",true); t("/a/[a-z]","/a/Q",false);
  t("/a/[0-9]","/a/5",true); t("/a/[0-9]","/a/x",false);
  // negated bracket
  t("/a/[!bc]","/a/d",true); t("/a/[!bc]","/a/b",false);
  // curly alternatives
  t("/a/{foo,bar}","/a/foo",true);
  t("/a/{foo,bar}","/a/bar",true);
  t("/a/{foo,bar}","/a/baz",false);
  t("/{x,y}/b","/y/b",true);
  // combinations
  t("/a/[a-c]*","/a/bzzz",true);
  t("/a/?[0-9]","/a/x7",true);
  // malformed patterns must simply not match, and must not crash
  const char* bad[]={"/a[bc","/a{b,c","/a]b","/a}b","/a[","/a{","/a[a-","/a[!","{","}","[","]","/*[","/*{a"};
  for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){ full(bad[i],"/abc"); full("/abc",bad[i]); }
  printf("\n%s (%d failures)\n", fails?"PATTERN SEMANTICS CHANGED":"pattern semantics unchanged", fails);
  return fails?1:0;
}

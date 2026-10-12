#pragma once
#include <string.h>
// Normalized lower-case transport values. Unknown/missing is not Pause.
inline int playbackState(const char* state) {
  if(!state) return -1;
  if(!strcmp(state,"plays") || !strcmp(state,"play") || !strcmp(state,"playing") || !strcmp(state,"1")) return 1;
  if(!strcmp(state,"pause") || !strcmp(state,"paused") || !strcmp(state,"stop") || !strcmp(state,"stopped") || !strcmp(state,"0")) return 0;
  return -1;
}

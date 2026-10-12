#pragma once
#include <string.h>
// Normalized lower-case transport values. Unknown/missing is not Pause.
inline int playbackState(const char* state) {
  if(!state) return -1;
  if(!strcmp(state,"plays") || !strcmp(state,"play") || !strcmp(state,"playing") || !strcmp(state,"1")) return 1;
  if(!strcmp(state,"pause") || !strcmp(state,"paused") || !strcmp(state,"stop") || !strcmp(state,"stopped") || !strcmp(state,"0")) return 0;
  return -1;
}

inline bool mediaIdentityChanged(const char* beforeUri,const char* uri,const char* beforeTitle,const char* title) {
  if(uri && *uri) return !beforeUri || strcmp(beforeUri,uri)!=0;
  return title && *title && beforeTitle && *beforeTitle && strcmp(beforeTitle,title)!=0;
}

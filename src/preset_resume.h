#pragma once
#include <cstdint>
struct PresetResume {
  bool pending=false;
  uint32_t due=0;
  void schedule(uint32_t now){pending=true;due=now+2000;}
  void cancel(){pending=false;}
  bool ready(uint32_t now)const{return pending && static_cast<int32_t>(now-due)>=0;}
  bool expired(uint32_t now)const{return ready(now) && uint32_t(now-due)>6000;}
};

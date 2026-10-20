#pragma once
#include <stdint.h>
struct DiscoveryLedger {
  uint8_t attempts[256] = {}, verified[256] = {}, pending[256] = {};
  uint16_t ports[256] = {};
  uint32_t due[256] = {};
  void offer(uint8_t host,uint16_t port,uint32_t now) {
    if (!host || host==255 || verified[host] || attempts[host]>=2 || pending[host]) return;
    ports[host]=port; due[host]=now; pending[host]=1;
  }
  int next(uint32_t now) const {
    for(int h=1;h<255;++h) if(pending[h] && int32_t(now-due[h])>=0) return h;
    return -1;
  }
  void begin(uint8_t host) { pending[host]=0; ++attempts[host]; }
  void finish(uint8_t host,bool ok,uint32_t now) {
    verified[host]=ok;
    if(!ok && attempts[host]<2) { pending[host]=1; due[host]=now+700; }
  }
  bool hasWork() const { for(int h=1;h<255;++h) if(pending[h]) return true; return false; }
};

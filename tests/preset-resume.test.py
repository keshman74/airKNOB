from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text();a=s.index('void servicePresetResume()');b=s.index('\nuint8_t* volumeArcCache',a)
code=r'''
#include <string>
#include <cassert>
#include <iostream>
#include "preset_resume.h"
using String=std::string;
PresetResume presetResume;String deviceIp="zone1",presetResumeZone="zone1";
bool cloud=true,isSlave=false,volumeDirty=false;const int WL_CONNECTED=1;int wifi=1;
struct Wifi{int status(){return wifi;}}WiFi;
struct Log{void println(const char*){}}Serial;
uint32_t now=100,lastPollAt=0,lastA31ActivityAt=0;const uint32_t kFallbackPollIntervalMs=4000,kPollIntervalMs=15000;
uint32_t millis(){return now;}bool isCloud(){return cloud;}
int reads=0,sends=0,state=0;bool sendOK=true;int activeEndpoint=0;
int readA33PresetPlayback(){++reads;return state;}
bool readEndpoint(const String&,int,const String&command,String&,uint32_t){assert(command=="setPlayerCmd:resume");++sends;return sendOK;}
'''+s[a:b]+r'''
void schedule(){presetResumeZone=deviceIp;presetResume.schedule(now);}
int main(){
 schedule();now+=1999;servicePresetResume();assert(reads==0&&sends==0);
 ++now;servicePresetResume();assert(reads==1&&sends==1&&!presetResume.pending);
 servicePresetResume();assert(sends==1);
 state=1;schedule();now+=2000;servicePresetResume();assert(reads==2&&sends==1);
 state=-1;schedule();now+=2000;servicePresetResume();assert(reads==3&&sends==2);
 schedule();presetResume.cancel();now+=2000;servicePresetResume();assert(sends==2);
 schedule();now+=1000;schedule();now+=1000;servicePresetResume();assert(sends==2);now+=1000;servicePresetResume();assert(sends==3);
 schedule();deviceIp="zone2";now+=2000;servicePresetResume();assert(!presetResume.pending&&sends==3);
 schedule();isSlave=true;now+=2000;servicePresetResume();assert(!presetResume.pending&&sends==3);isSlave=false;
 schedule();volumeDirty=true;now+=2000;servicePresetResume();assert(presetResume.pending&&sends==3);volumeDirty=false;servicePresetResume();assert(sends==4);
 schedule();wifi=0;now+=9000;servicePresetResume();assert(!presetResume.pending&&sends==4);wifi=1;
 sendOK=false;state=0;schedule();now+=2000;servicePresetResume();servicePresetResume();assert(sends==5);
 now=0xffffff00;schedule();now+=2000;servicePresetResume();assert(sends==6);
 cloud=false;schedule();now+=2000;servicePresetResume();assert(sends==6);
 std::cout<<"Actual A33 preset scheduler: 2s wait, playing skip, unknown fallback, one shot, replace/cancel, zone/slave guards, volume priority, expiry and wrap passed\n";
}
'''
Path('/tmp/knob_preset_resume.cpp').write_text(code)
subprocess.run(['g++','-std=c++11','-Isrc','/tmp/knob_preset_resume.cpp','-o','/tmp/knob_preset_resume'],check=True)
subprocess.run(['/tmp/knob_preset_resume'],check=True)

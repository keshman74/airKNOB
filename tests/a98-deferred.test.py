from pathlib import Path
import subprocess
s=Path("src/main.cpp").read_text()
a=s.index("void requestWiiMPresetRefresh()")
b=s.index("void setMenu(",a)
p=s.index("bool isWiiMPresetChip()");q=s.index("bool sendWiiMPreset(",p)
code=r'''
#include <string>
#include <atomic>
#include <cassert>
#include <iostream>
#include <cstdint>
using String=std::string;
enum class Chip{A31,A33,A97,A98};
struct Endpoint{Chip chip=Chip::A98;}activeEndpoint;
enum class MenuScreen{Player,Presets};
MenuScreen menuScreen=MenuScreen::Presets;
bool wiimPresetReadPending=false,otaActive=false,rebootAfterResponse=false;
uint32_t zoneEpoch=1,wiimPresetReadEpoch=0,lastA31ActivityAt=0,a98MetadataRetry=0,now=1000;
String deviceIp="192.168.0.21",wiimPresetReadIp,presetZone=deviceIp,menuMessage;
std::atomic<bool> artworkCancel{false};
int devicePresetCount=4,selectedItem=0,scanPhase=0,reads=0,draws=0;
bool busy=false,localVolume=false,readOK=true;
uint32_t millis(){return now;}
bool localVolumePending(){return localVolume;}
bool backgroundPollBusy(){return busy;}
void drawMenu(){++draws;}
bool readDevicePresets(){assert(!busy&&!otaActive);++reads;devicePresetCount=readOK?4:0;return readOK;}
'''+s[p:q]+s[a:b]+r'''
void reset(Chip chip){wiimPresetReadPending=false;activeEndpoint.chip=chip;menuScreen=MenuScreen::Presets;
 zoneEpoch=1;deviceIp="192.168.0.21";presetZone=deviceIp;busy=localVolume=otaActive=rebootAfterResponse=false;scanPhase=0;readOK=true;a98MetadataRetry=0;devicePresetCount=4;}
int main(){
 for(Chip chip:{Chip::A97,Chip::A98}){
  reset(chip);int before=reads;
  busy=true;requestWiiMPresetRefresh();assert(reads==before&&wiimPresetReadPending&&menuMessage=="READING"&&artworkCancel);
  serviceWiiMPresetRefresh();assert(reads==before&&wiimPresetReadPending);
  busy=false;serviceWiiMPresetRefresh();assert(reads==before+1&&!wiimPresetReadPending&&menuMessage.empty()&&devicePresetCount==4);
  serviceWiiMPresetRefresh();assert(reads==before+1);
  for(int gate=0;gate<4;++gate){
   reset(chip);requestWiiMPresetRefresh();if(gate==0)otaActive=true;if(gate==1)rebootAfterResponse=true;if(gate==2)scanPhase=1;if(gate==3)localVolume=true;
   before=reads;serviceWiiMPresetRefresh();assert(reads==before&&wiimPresetReadPending);
  }
  for(int changed=0;changed<4;++changed){
   reset(chip);requestWiiMPresetRefresh();if(changed==0)menuScreen=MenuScreen::Player;if(changed==1)++zoneEpoch;if(changed==2)deviceIp="192.168.0.22";if(changed==3)activeEndpoint.chip=Chip::A33;
   before=reads;serviceWiiMPresetRefresh();assert(reads==before&&!wiimPresetReadPending);
  }
  reset(chip);readOK=false;requestWiiMPresetRefresh();serviceWiiMPresetRefresh();
  assert(menuMessage=="READ FAILED"&&!wiimPresetReadPending&&lastA31ActivityAt==now);
  assert(a98MetadataRetry==(chip==Chip::A98?now+30000:0));
  reset(chip);presetZone="previous-zone";requestWiiMPresetRefresh();assert(devicePresetCount==0);
 }
 for(Chip chip:{Chip::A31,Chip::A33}){
  reset(chip);int before=draws;requestWiiMPresetRefresh();assert(!wiimPresetReadPending&&draws==before);
 }
 std::cout<<"Deferred A97/A98 reads: no overlap, one-shot, cancellation, OTA/volume/scan gates, retry pacing and chip isolation passed\n";
}
'''
Path("/tmp/a98_deferred.cpp").write_text(code)
subprocess.run(["g++","-std=c++17","/tmp/a98_deferred.cpp","-o","/tmp/a98_deferred"],check=True)
subprocess.run(["/tmp/a98_deferred"],check=True)

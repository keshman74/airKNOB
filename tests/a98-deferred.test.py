from pathlib import Path
import subprocess
s=Path("src/main.cpp").read_text()
a=s.index("void requestA98PresetRefresh()")
b=s.index("void setMenu(",a)
code=r'''
#include <string>
#include <atomic>
#include <cassert>
#include <iostream>
#include <cstdint>
using String=std::string;
enum class Chip{A31,A33,A98};
struct Endpoint{Chip chip=Chip::A98;}activeEndpoint;
enum class MenuScreen{Player,Presets};
MenuScreen menuScreen=MenuScreen::Presets;
bool a98PresetReadPending=false,otaActive=false,rebootAfterResponse=false;
uint32_t zoneEpoch=1,a98PresetReadEpoch=0,lastA31ActivityAt=0,a98MetadataRetry=0,now=1000;
String deviceIp="192.168.0.21",a98PresetReadIp,presetZone=deviceIp,menuMessage;
std::atomic<bool> artworkCancel{false};
int devicePresetCount=4,selectedItem=0,scanPhase=0,reads=0,draws=0;
bool busy=false,localVolume=false,readOK=true;
uint32_t millis(){return now;}
bool localVolumePending(){return localVolume;}
bool backgroundPollBusy(){return busy;}
void drawMenu(){++draws;}
bool readDevicePresets(){assert(!busy);assert(!otaActive);++reads;devicePresetCount=readOK?4:0;return readOK;}
'''+s[a:b]+r'''
void reset(){a98PresetReadPending=false;activeEndpoint.chip=Chip::A98;menuScreen=MenuScreen::Presets;
 zoneEpoch=1;deviceIp="192.168.0.21";presetZone=deviceIp;busy=localVolume=otaActive=rebootAfterResponse=false;scanPhase=0;readOK=true;}
int main(){
 // Opening is nonblocking even while a slow status/artwork request is active.
 busy=true;requestA98PresetRefresh();assert(reads==0&&a98PresetReadPending&&menuMessage=="READING"&&artworkCancel);
 serviceA98PresetRefresh();assert(reads==0&&a98PresetReadPending);
 busy=false;serviceA98PresetRefresh();assert(reads==1&&!a98PresetReadPending&&menuMessage.empty()&&devicePresetCount==4);
 serviceA98PresetRefresh();assert(reads==1); // one read per request
 for(int gate=0;gate<4;++gate){
  reset();requestA98PresetRefresh();if(gate==0)otaActive=true;if(gate==1)rebootAfterResponse=true;if(gate==2)scanPhase=1;if(gate==3)localVolume=true;
  int before=reads;serviceA98PresetRefresh();assert(reads==before&&a98PresetReadPending);
 }
 for(int changed=0;changed<4;++changed){
  reset();requestA98PresetRefresh();if(changed==0)menuScreen=MenuScreen::Player;if(changed==1)++zoneEpoch;if(changed==2)deviceIp="192.168.0.22";if(changed==3)activeEndpoint.chip=Chip::A33;
  int before=reads;serviceA98PresetRefresh();assert(reads==before&&!a98PresetReadPending);
 }
 reset();readOK=false;requestA98PresetRefresh();serviceA98PresetRefresh();
 assert(menuMessage=="READ FAILED"&&!a98PresetReadPending&&lastA31ActivityAt==now&&a98MetadataRetry==now+30000);
 reset();presetZone="previous-zone";requestA98PresetRefresh();assert(devicePresetCount==0);
 reset();activeEndpoint.chip=Chip::A31;int before=draws;requestA98PresetRefresh();assert(!a98PresetReadPending&&draws==before);
 activeEndpoint.chip=Chip::A33;requestA98PresetRefresh();assert(!a98PresetReadPending&&draws==before);
 std::cout<<"Deferred A98 reads: no parallel request, one-shot, cancellation, OTA/volume/scan gates, retry pacing and chip isolation passed\n";
}
'''
Path("/tmp/a98_deferred.cpp").write_text(code)
subprocess.run(["g++","-std=c++17","/tmp/a98_deferred.cpp","-o","/tmp/a98_deferred"],check=True)
subprocess.run(["/tmp/a98_deferred"],check=True)

from pathlib import Path
import os,subprocess
s=Path('src/main.cpp').read_text()
mock=Path('tests/xml-media.test.py').read_text().split("code=r'''",1)[1].split("'''+s[a:b]",1)[0]
mock=mock.replace(' int indexOf(char c',' void toLowerCase(){for(char&c:*this)c=char(std::tolower((unsigned char)c));}\n int indexOf(char c')
a=s.index('struct BackgroundPoll {');b=s.index('struct ZoneInfo',a)
c=s.index('void backgroundPollWorker(');d=s.index('\nint zoneCount',c)
e=s.index('void serviceBackgroundPoll()');f=s.index('\nvoid sendVolumeWhenReady()',e)
code=mock+r'''
#include <atomic>
#include <vector>
#include <ArduinoJson.h>
#include "media_state.h"
void convertFromJson(JsonVariantConst source,String& target){target=source.as<const char*>()?source.as<const char*>():"";}
bool canConvertFromJson(JsonVariantConst source,const String&){return source.is<const char*>();}
enum class Chip{Unknown,A31,A33,A97,A98};
struct Endpoint{Chip chip=Chip::Unknown;uint16_t port=80;bool tls=false;};Endpoint activeEndpoint;
using TaskHandle_t=void*;
uint32_t now=10000,zoneEpoch=1,controlEpoch=0,a98ArtworkEpoch=0,lastA31ActivityAt=0,lastPollAt=0;
const uint32_t kPollIntervalMs=15000;uint32_t millis(){return now;}
String deviceIp="192.168.0.35";bool otaActive=false,rebootAfterResponse=false;
bool isCloud(){return activeEndpoint.chip==Chip::A33;}
const int pdPASS=1,pdTRUE=1,portMAX_DELAY=0;bool createOK=true;int notifications=0;
int xTaskCreatePinnedToCore(void(*)(void*),const char*,int,void*,int,TaskHandle_t*handle,int){if(createOK)*handle=(void*)1;return createOK?pdPASS:0;}
void xTaskNotifyGive(TaskHandle_t){++notifications;}
struct Done{};int wake=0;void ulTaskNotifyTake(int,int){if(wake++)throw Done{};}
std::vector<String> calls,ips;String offered=R"({"PlayState":"plays"})",extraOffered=R"({"PlayStatus":"pause"})";bool readOK=true;
bool readEndpoint(const String&ip,const Endpoint&,const String&cmd,String&body,uint32_t timeout){assert(timeout<=800);ips.push_back(ip);calls.push_back(cmd);body=cmd=="getPlayerStatus"?extraOffered:offered;now+=20;return readOK;}
void runArtworkJob(){}void commitArtworkJob(){}
int playerResults=0,groupResults=0;String receivedExtra;
void pollDevice(const String*,bool,const String*extra){++playerResults;receivedExtra=*extra;}
bool refreshGroupStatus(const String*,bool){++groupResults;return true;}
'''+s[a:b]+s[c:d]+s[e:f]+r'''
void runWorker(){wake=0;try{backgroundPollWorker(nullptr);}catch(Done&){}assert(backgroundPoll.state.load()==3);}
int main(){
 assert(!startBackgroundPoll(false));
 activeEndpoint.chip=Chip::A31;createOK=false;assert(!startBackgroundPoll(false));createOK=true;
 for(Chip chip:{Chip::A31,Chip::A33,Chip::A97,Chip::A98}){
  activeEndpoint.chip=chip;calls.clear();assert(startBackgroundPoll(false));assert(backgroundPollBusy());assert(!startBackgroundPoll(true));runWorker();assert(calls.size()==1);assert(calls[0]==(chip==Chip::A33?"getStatusEx":"getPlayerStatus"));int before=playerResults;serviceBackgroundPoll();assert(playerResults==before+1&&!backgroundPollBusy());
 }
 activeEndpoint.chip=Chip::A33;offered=R"({"AllMate":{"TrackTitle":"test"}})";calls.clear();assert(startBackgroundPoll(false));runWorker();assert(calls.size()==2);serviceBackgroundPoll();assert(receivedExtra==extraOffered);
 assert(startBackgroundPoll(true));runWorker();serviceBackgroundPoll();assert(groupResults==1);
 activeEndpoint.chip=Chip::A31;int before=playerResults;assert(startBackgroundPoll(false));++zoneEpoch;deviceIp="192.168.0.21";runWorker();assert(ips.back()=="192.168.0.35");serviceBackgroundPoll();assert(playerResults==before&&!backgroundPollBusy());
 assert(startBackgroundPoll(false));++controlEpoch;runWorker();serviceBackgroundPoll();assert(playerResults==before);
 assert(startBackgroundPoll(false));runWorker();otaActive=true;serviceBackgroundPoll();assert(playerResults==before);otaActive=false;
 assert(startBackgroundPoll(false));runWorker();serviceBackgroundPoll();assert(playerResults==before+1);
 std::cout<<"Actual background worker: all chips, single-flight, A33 fallback, old zone/control/OTA result rejection passed\n";
}
'''
Path('/tmp/knob_background.cpp').write_text(code)
include=os.environ.get('ARDUINOJSON_TEST_INCLUDE','.pio/libdeps/onx2424g013/ArduinoJson/src')
subprocess.run(['g++','-std=c++17','-Isrc','-I'+include,'/tmp/knob_background.cpp','-o','/tmp/knob_background'],check=True)
subprocess.run(['/tmp/knob_background'],check=True)

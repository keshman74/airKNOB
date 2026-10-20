from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text()
mock=Path('tests/xml-media.test.py').read_text().split("code=r'''",1)[1].split("'''+s[a:b]",1)[0]
mock=mock.replace(' int indexOf(char c',' String(int n):std::string(std::to_string(n)){}\n int indexOf(char c')
a=s.index('struct BackgroundPoll {');b=s.index('struct ZoneInfo',a)
c=s.index('void runWiiMVolumeJob()');d=s.index('bool readWorkerEndpoint(',c)
e=s.index('void commitWiiMVolumeJob()');f=s.index('void serviceBackgroundPoll()',e)
g=s.index('void sendVolumeWhenReady()');h=s.index('int readA33PresetPlayback()',g)
code=mock+r'''
#include <atomic>
#include <vector>
enum class Chip{Unknown,A31,A33,A97,A98};
struct Endpoint{Chip chip=Chip::A97;uint16_t port=443;bool tls=true;};Endpoint activeEndpoint;
using TaskHandle_t=void*;
uint32_t now=10000,controlEpoch=0,a98ArtworkEpoch=0,lastA31ActivityAt=0,lastPollAt=0;
std::atomic<uint32_t> zoneEpoch{1};
const uint32_t kPollIntervalMs=15000,kVolumeSendDelayMs=80,kVolumeRetryDelayMs=2000;
uint32_t lastVolumeSentAt=0,volumeRetryAt=0,lastTcpSentAt=0;
String deviceIp="192.168.0.99";
bool otaActive=false,rebootAfterResponse=false,volumeDirty=true,deviceOnline=false;
int volume=20,volumeFailures=0,draws=0,legacy=0,notifications=0;
std::atomic<bool> artworkCancel{false};
uint32_t millis(){return now;}
void drawScreen(){++draws;}
struct {template<class...T>void printf(const char*,T...){}void println(const char*){}}Serial;
const int WL_CONNECTED=3,pdPASS=1;bool online=true,createOK=true;
struct {int status(){return online?WL_CONNECTED:0;}}WiFi;
void backgroundPollWorker(void*){}
int xTaskCreatePinnedToCore(void(*)(void*),const char*,int,void*,int,TaskHandle_t*h,int){if(createOK)*h=(void*)1;return createOK?pdPASS:0;}
void xTaskNotifyGive(TaskHandle_t){++notifications;}
bool mcuCommand(const char*){++legacy;return true;}
bool request(const String&,String&){++legacy;return true;}
struct Session{
 void close(){}
 bool ok=true;std::vector<String>commands;
 bool read(const String&,uint16_t,uint32_t,const String&cmd,String&,uint32_t){commands.push_back(cmd);now+=120;return ok;}
}wiimWorkerHttp;
struct Soap {
 bool ok=false;std::vector<int> targets;
 bool set(const String&,uint32_t,int n){targets.push_back(n);if(ok)now+=15;return ok;}
}a97VolumeSoap;
std::atomic<int> wiimVolumePath{0};
std::atomic<uint32_t> wiimVolumeSends{0},wiimVolumeLastMs{0},wiimVolumeMaxMs{0};
'''+s[a:b]+s[c:d]+s[e:f]+s[g:h]+r'''
void finish(){backgroundPoll.state.store(2);runWiiMVolumeJob();backgroundPoll.state.store(3);commitWiiMVolumeJob();backgroundPoll.state.store(0);}
void target(int n){volume=n;volumeDirty=true;volumeFailures=0;volumeRetryAt=0;}
int main(){
 // Dispatch does not perform network IO on the UI thread.
 sendVolumeWhenReady();assert(backgroundPollBusy()&&wiimWorkerHttp.commands.empty()&&legacy==0);
 // Twenty turns during one request leave exactly the latest absolute target.
 for(int n=22;n<=60;n+=2){target(n);sendVolumeWhenReady();}
 assert(notifications==1&&backgroundPoll.volumeTarget==20);
 finish();assert(volumeDirty&&volume==60&&wiimWorkerHttp.commands.size()==1);
 sendVolumeWhenReady();assert(backgroundPoll.volumeTarget==60);finish();
 assert(!volumeDirty&&wiimWorkerHttp.commands.size()==2&&wiimWorkerHttp.commands.back()=="setPlayerCmd:vol:60");
 assert(wiimVolumeLastMs==120&&wiimVolumeMaxMs==120&&deviceOnline);
 // A play/pause or artwork change does not strand an acknowledged volume.
 now+=200;target(40);sendVolumeWhenReady();++controlEpoch;++a98ArtworkEpoch;finish();assert(!volumeDirty);
 // A job for a previous zone must neither send nor change its replacement.
 now+=200;target(42);sendVolumeWhenReady();++zoneEpoch;auto sent=wiimWorkerHttp.commands.size();finish();assert(wiimWorkerHttp.commands.size()==sent&&volumeDirty);
 // Failure of an old target must not delay the newer one.
 now+=200;target(44);sendVolumeWhenReady();target(46);wiimWorkerHttp.ok=false;finish();
 assert(volumeDirty&&volumeRetryAt==0&&volumeFailures==0);
 sendVolumeWhenReady();finish();assert(volumeDirty&&volumeFailures==1&&volumeRetryAt==now+2000);
 now=volumeRetryAt;sendVolumeWhenReady();finish();assert(volumeFailures==2);
 now=volumeRetryAt;sendVolumeWhenReady();finish();assert(!volumeDirty&&volumeFailures==0);
 wiimWorkerHttp.ok=true;
 // Artwork is cancelled cooperatively; never run a second worker job.
 backgroundPoll.state.store(2);backgroundPoll.artwork=true;target(50);sendVolumeWhenReady();assert(artworkCancel&&backgroundPoll.state==2);
 backgroundPoll.state.store(0);backgroundPoll.artwork=false;
 // No network, task allocation failure and OTA leave the target available.
 now+=200;online=false;sendVolumeWhenReady();assert(!backgroundPollBusy()&&volumeDirty);online=true;
 backgroundPollTask=nullptr;createOK=false;sendVolumeWhenReady();assert(!backgroundPollBusy());createOK=true;
 otaActive=true;sendVolumeWhenReady();assert(!backgroundPollBusy());otaActive=false;
 sendVolumeWhenReady();otaActive=true;sent=wiimWorkerHttp.commands.size();finish();assert(wiimWorkerHttp.commands.size()==sent&&volumeDirty);otaActive=false;
 // A98 uses the same path; successful stale-zone completion cannot commit.
 activeEndpoint.chip=Chip::A98;now+=200;sendVolumeWhenReady();backgroundPoll.state.store(2);runWiiMVolumeJob();++zoneEpoch;
 backgroundPoll.state.store(3);commitWiiMVolumeJob();assert(volumeDirty);backgroundPoll.state.store(0);
 now+=200;sendVolumeWhenReady();finish();assert(!volumeDirty);
 // A31/A33 retain their existing transport path.
 for(auto chip:{Chip::A31,Chip::A33}){activeEndpoint.chip=chip;now+=200;target(32);sendVolumeWhenReady();assert(!volumeDirty);}
 assert(legacy==2);
 // A97 SOAP success bypasses HTTPS; A98 stays on its verified HTTPS route.
 activeEndpoint.chip=Chip::A97;a97VolumeSoap.ok=true;now+=200;
 auto httpsBefore=wiimWorkerHttp.commands.size();target(38);sendVolumeWhenReady();finish();
 assert(!volumeDirty&&wiimVolumePath==2&&wiimVolumeLastMs==15);
 assert(wiimWorkerHttp.commands.size()==httpsBefore&&a97VolumeSoap.targets.back()==38);
 activeEndpoint.chip=Chip::A98;now+=200;target(40);auto soapBefore=a97VolumeSoap.targets.size();
 sendVolumeWhenReady();finish();assert(!volumeDirty&&wiimVolumePath==1);
 assert(a97VolumeSoap.targets.size()==soapBefore&&wiimWorkerHttp.commands.size()==httpsBefore+1);
 std::cout<<"WiiM volume: async dispatch, latest target, stale zone/epoch, failures, retry bounds, artwork cancellation, OTA and legacy transports passed\n";
}
'''
Path('/tmp/knob_wiim_volume.cpp').write_text(code)
subprocess.run(['g++','-std=c++17','-Isrc','/tmp/knob_wiim_volume.cpp','-o','/tmp/knob_wiim_volume'],check=True)
subprocess.run(['/tmp/knob_wiim_volume'],check=True)

session=mock+r'''
#include <atomic>
#include <vector>
uint32_t now=10000;uint32_t millis(){return now;}
const int WL_CONNECTED=3;bool online=true;
struct {int status(){return online?WL_CONNECTED:0;}}WiFi;
struct IPAddress{bool fromString(const String&s){return s.find("192.168.")==0;}};
int opens=0,gets=0,stops=0,nextStatus=200;bool serverKeeps=true,bodyOK=true;String offered="OK";
std::vector<String> urls;
struct WiFiClientSecure{
 bool live=false;void stop(){if(live)++stops;live=false;}
 bool connected(){return live;}void setInsecure(){}void setHandshakeTimeout(int n){assert(n==2);}
};
struct HTTPClient{
 WiFiClientSecure*client=nullptr;bool reuse=false;
 void setReuse(bool b){reuse=b;}void useHTTP10(bool b){assert(!b);}
 void setConnectTimeout(int n){assert(n==350);}void setTimeout(uint32_t n){assert(n==800);}
 bool begin(WiFiClientSecure&c,const String&url){client=&c;urls.push_back(url);return true;}
 int GET(){++gets;if(!client->live){++opens;client->live=true;}return nextStatus;}
 void end(){if(client&&(!reuse||!serverKeeps))client->stop();}
};
String encodeCommand(String s){return s;}
bool readBoundedResponse(HTTPClient&,String&body,size_t limit,uint32_t budget){assert(limit==65536&&budget==1500);body=offered;return bodyOK;}
bool explicitApiRejection(const std::string&s){return s=="unknown command";}
#include "wiim_worker_http.h"
int main(){
 WiiMWorkerHttp session;String body;
 assert(session.read("192.168.0.99",443,1,"getPlayerStatus",body,800));assert(opens==1&&session.reused==0);
 assert(session.read("192.168.0.99",443,1,"setPlayerCmd:vol:20",body,800));assert(opens==1&&session.reused==1);
 assert(urls.back()=="https://192.168.0.99:443/httpapi.asp?command=setPlayerCmd:vol:20");
 // Host, port and generation each invalidate the retained socket.
 assert(session.read("192.168.0.21",443,1,"getPlayerStatus",body,800));assert(opens==2);
 assert(session.read("192.168.0.21",8443,1,"getPlayerStatus",body,800));assert(opens==3);
 assert(session.read("192.168.0.21",8443,2,"getPlayerStatus",body,800));assert(opens==4);
 // Honour Connection: close: subsequent request creates a fresh connection.
 serverKeeps=false;assert(session.read("192.168.0.21",8443,2,"getPlayerStatus",body,800));
 auto count=opens;assert(session.read("192.168.0.21",8443,2,"getPlayerStatus",body,800));assert(opens==count+1);
 serverKeeps=true;nextStatus=-1;int requests=gets;
 assert(!session.read("192.168.0.21",8443,2,"setPlayerCmd:vol:40",body,800));assert(gets==requests+1); // no automatic replay
 nextStatus=200;bodyOK=false;assert(!session.read("192.168.0.21",8443,2,"getPlayerStatus",body,800));
 bodyOK=true;offered="unknown command";assert(!session.read("192.168.0.21",8443,2,"getPlayerStatus",body,800));
 offered="OK";assert(session.read("192.168.0.21",8443,2,"getPlayerStatus",body,800));session.close();
 count=opens;assert(session.read("192.168.0.21",8443,2,"getPlayerStatus",body,800));assert(opens==count+1);
 online=false;requests=gets;assert(!session.read("192.168.0.21",8443,2,"getPlayerStatus",body,800));assert(gets==requests);
 online=true;assert(!session.read("invalid",443,2,"getPlayerStatus",body,800));assert(gets==requests);
 std::cout<<"WiiM HTTPS: keep-alive, server close, host/port/epoch isolation, explicit close, bounded-read failure, API rejection and no replay passed\n";
}
'''
Path('/tmp/knob_wiim_session.cpp').write_text(session)
subprocess.run(['g++','-std=c++17','-Isrc','/tmp/knob_wiim_session.cpp','-o','/tmp/knob_wiim_session'],check=True)
subprocess.run(['/tmp/knob_wiim_session'],check=True)

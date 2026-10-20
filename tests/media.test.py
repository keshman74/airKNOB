from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text()
poll=s[s.index('void commitArtworkJob() {'):s.index('void pollArtwork() {')]
a=s.index('  const bool feedbackActive=');b=s.index('  if (artworkReady)',a);feedback=s[a:b]
a=s.index('void invalidateArtwork()');b=s.index('int pngLine(',a);updates=s[a:b]
code=r'''
#include <string>
#include <atomic>
#include <cstring>
#include <vector>
#include <cassert>
#include <cstdint>
#include <iostream>
#include "media_state.h"
struct String:std::string{using std::string::string;String(const std::string&s):std::string(s){}String(){}bool isEmpty()const{return empty();}};
enum class MenuScreen{Player,Zone};MenuScreen menuScreen=MenuScreen::Player;
const int WL_CONNECTED=1,TFT_WHITE=65535,TFT_BLACK=0;int wifiState=1;struct Wifi{int status(){return wifiState;}} WiFi;
std::atomic<bool> artworkCancel{false};
uint16_t artPixels[4]={},nextArtPixels[4]={1,2,3,4};
struct ArtJob{String title,body;bool ok=false,unchanged=false;}backgroundPoll;
uint32_t a98ArtworkEpoch=0,zoneEpoch=0;bool otaActive=false,rebootAfterResponse=false;
enum class Chip{A31,A98};struct Endpoint{Chip chip=Chip::A98;}activeEndpoint;bool localVolumePending(){return false;}
uint32_t now=10000,lastA31ActivityAt=0,lastArtAttemptAt=0,feedbackUntil=12000;uint32_t millis(){return now;}
uint32_t marqueeStartedAt=0,marqueeLastAt=0,lastArtworkPollAt=0;int marqueeOffset=0;const uint32_t kArtworkPollMs=20000;
String currentArtworkUrl="old",lastArtAttemptUrl,offeredUrl,fallbackUrl,reportedArtworkUrl,deviceIp="192.168.0.35",trackTitle="old song",trackArtist="old artist",currentMediaUri="old URI";
bool artworkReady=true,artworkAttempted=true,cloud=false,decodeOk=false,deviceOnline=true,groupKnown=false,isSlave=false,playing=true;
int feedbackKind=0,draws=0,downloads=0,maxDrawY=0;
struct Logger{void println(const char*){}} Serial;
struct Display{uint16_t color565(int,int,int){return 1;}void fillRect(int,int y,int,int h,int){maxDrawY=std::max(maxDrawY,y+h);}void drawLine(int,int y,int,int y2,int){maxDrawY=std::max(maxDrawY,std::max(y,y2)+1);}}display;
uint16_t accentColor(){return 1;}void drawWindow(const String&,int,int y,int,int,int,int,int){maxDrawY=std::max(maxDrawY,y+18);}
String cleanArtworkUrl(const String&s){return s;}String fetchArtworkAddress(){return offeredUrl;}String fetchSoapArtworkAddress(){return fallbackUrl;}
bool isCloud(){return cloud;}bool downloadAndDecodeArt(const String&){++downloads;return decodeOk;}void drawScreen(){++draws;}
'''+updates+poll+'void drawFeedback(){\n'+feedback+'}\n'+r'''
int main(){
 for(const char*s:{"plays","play","playing","1"})assert(playbackState(s)==1);
 for(const char*s:{"pause","paused","stop","stopped","0"})assert(playbackState(s)==0);
 for(const char*s:{"","unknown","buffering","null","2"})assert(playbackState(s)==-1);
 backgroundPoll.title=trackTitle;backgroundPoll.body="";commitArtworkJob();assert(!artworkReady&&currentArtworkUrl.empty()&&draws==1);
 artworkReady=true;currentArtworkUrl="old";backgroundPoll.body="new";backgroundPoll.ok=false;commitArtworkJob();assert(!artworkReady&&currentArtworkUrl.empty());
 backgroundPoll.ok=true;commitArtworkJob();assert(artworkReady&&currentArtworkUrl=="new"&&artPixels[3]==4);
 backgroundPoll.body="wrong";backgroundPoll.title="different";commitArtworkJob();assert(currentArtworkUrl=="new");
 backgroundPoll.title=trackTitle;artworkCancel.store(true);commitArtworkJob();assert(currentArtworkUrl=="new");artworkCancel.store(false);
 assert(updateTrackMetadata("New station","","new URI"));assert(trackArtist.empty()&&!artworkReady&&currentArtworkUrl.empty());
 artworkReady=true;currentArtworkUrl="new";assert(updateTrackMetadata("Another song","","new URI"));assert(artworkReady); // same station/URI, ICY title only
 assert(updateTrackMetadata("Another song","","other URI"));assert(!artworkReady); // URI changes even if title is the same
 trackArtist="old artist";assert(updateTrackMetadata("Another song","","other URI"));assert(trackArtist.empty());
 artworkCancel.store(false);backgroundPoll.title=trackTitle;backgroundPoll.body="new station cover";backgroundPoll.ok=true;commitArtworkJob();assert(artworkReady);
 backgroundPoll.body="";backgroundPoll.ok=false;commitArtworkJob();assert(!artworkReady&&currentArtworkUrl.empty());
 now=10000;feedbackUntil=12000;
 for(int kind=1;kind<=3;++kind){feedbackKind=kind;maxDrawY=0;drawFeedback();assert(maxDrawY<=62);}
 groupKnown=isSlave=true;for(int kind=1;kind<=2;++kind){feedbackKind=kind;maxDrawY=0;drawFeedback();assert(maxDrawY<=62);}
 std::cout<<"Actual media handling: no stale cover on absence/failure/new station, empty artist clears, URI identity, retry and header bounds passed\n";
}
'''
Path('/tmp/knob_media.cpp').write_text(code)
subprocess.run(['g++','-std=c++11','-I','src','/tmp/knob_media.cpp','-o','/tmp/knob_media'],check=True)
subprocess.run(['/tmp/knob_media'],check=True)

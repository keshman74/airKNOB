from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text()
poll=s[s.index('void pollArtwork() {'):s.index('\nString decodeMetadata')]
a=s.index('  const bool feedbackActive=');b=s.index('  if (artworkReady)',a);feedback=s[a:b]
code=r'''
#include <string>
#include <vector>
#include <cassert>
#include <cstdint>
#include <iostream>
#include "media_state.h"
struct String:std::string{using std::string::string;String(const std::string&s):std::string(s){}String(){}bool isEmpty()const{return empty();}};
enum class MenuScreen{Player,Zone};MenuScreen menuScreen=MenuScreen::Player;
const int WL_CONNECTED=1,TFT_WHITE=65535,TFT_BLACK=0;int wifiState=1;struct Wifi{int status(){return wifiState;}} WiFi;
uint32_t now=10000,lastA31ActivityAt=0,lastArtAttemptAt=0,feedbackUntil=12000;uint32_t millis(){return now;}
String currentArtworkUrl="old",lastArtAttemptUrl,offeredUrl,fallbackUrl;
bool artworkReady=true,artworkAttempted=true,cloud=false,decodeOk=false,deviceOnline=true,groupKnown=false,isSlave=false,playing=true;
int feedbackKind=0,draws=0,downloads=0,maxDrawY=0;
struct Logger{void println(const char*){}} Serial;
struct Display{uint16_t color565(int,int,int){return 1;}void fillRect(int,int y,int,int h,int){maxDrawY=std::max(maxDrawY,y+h);}void drawLine(int,int y,int,int y2,int){maxDrawY=std::max(maxDrawY,std::max(y,y2)+1);}}display;
uint16_t accentColor(){return 1;}void drawWindow(const String&,int,int y,int,int,int,int,int){maxDrawY=std::max(maxDrawY,y+18);}
String cleanArtworkUrl(const String&s){return s;}String fetchArtworkAddress(){return offeredUrl;}String fetchSoapArtworkAddress(){return fallbackUrl;}
bool isCloud(){return cloud;}bool downloadAndDecodeArt(const String&){++downloads;return decodeOk;}void drawScreen(){++draws;}
'''+poll+'void drawFeedback(){\n'+feedback+'}\n'+r'''
int main(){
 for(const char*s:{"plays","play","playing","1"})assert(playbackState(s)==1);
 for(const char*s:{"pause","paused","stop","stopped","0"})assert(playbackState(s)==0);
 for(const char*s:{"","unknown","buffering","null","2"})assert(playbackState(s)==-1);
 offeredUrl="";pollArtwork();assert(artworkReady&&currentArtworkUrl=="old"&&downloads==0&&draws==0);
 offeredUrl="new";decodeOk=false;pollArtwork();assert(artworkReady&&currentArtworkUrl=="old"&&draws==0);
 int attempts=downloads;now+=1000;pollArtwork();assert(downloads==attempts);
 now+=5000;decodeOk=true;pollArtwork();assert(artworkReady&&currentArtworkUrl=="new"&&draws==1);
 attempts=downloads;now+=30000;pollArtwork();assert(downloads==attempts);
 offeredUrl="unreachable";fallbackUrl="new";decodeOk=false;pollArtwork();assert(artworkReady&&currentArtworkUrl=="new"&&draws==1);
 artworkReady=false;offeredUrl="new";lastArtAttemptUrl="";decodeOk=true;pollArtwork();assert(artworkReady&&draws==2);
 now=10000;feedbackUntil=12000;
 for(int kind=1;kind<=3;++kind){feedbackKind=kind;maxDrawY=0;drawFeedback();assert(maxDrawY<=62);}
 groupKnown=isSlave=true;for(int kind=1;kind<=2;++kind){feedbackKind=kind;maxDrawY=0;drawFeedback();assert(maxDrawY<=62);}
 std::cout<<"Actual artwork poll and feedback: empty/failed retention, retry, atomic commit selection, playback aliases, unknown state and cover bounds passed\n";
}
'''
Path('/tmp/knob_media.cpp').write_text(code)
subprocess.run(['g++','-std=c++11','-I','src','/tmp/knob_media.cpp','-o','/tmp/knob_media'],check=True)
subprocess.run(['/tmp/knob_media'],check=True)

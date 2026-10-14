from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text();a=s.index('void pollA98Events() {');b=s.index('bool a98ReadKeyMapping(',a)
code=r'''
#include <string>
#include <vector>
#include <memory>
#include <cassert>
#include <cstring>
#include <iostream>
#include "a98_presets.h"
struct String:std::string{using std::string::string;String(){}String(const std::string& s):std::string(s){}String(unsigned n):std::string(std::to_string(n)){}String(int n):std::string(std::to_string(n)){}bool isEmpty()const{return empty();}};
struct Socket{std::string input,output,ip="zone";size_t cursor=0;bool connected=true;};
struct IP{String address;String toString(){return address;}};
struct WiFiClient{std::shared_ptr<Socket> s;explicit operator bool()const{return s&&s->connected;}int available(){return s&&s->connected?int(s->input.size()-s->cursor):0;}int read(){return s->input[s->cursor++];}void print(const char*str){s->output+=str;}void stop(){if(s)s->connected=false;}IP remoteIP(){return IP{String(s->ip)};}};
struct Server{std::vector<std::shared_ptr<Socket>> sockets;WiFiClient available(){if(sockets.empty())return {};WiFiClient c;c.s=sockets.front();sockets.erase(sockets.begin());return c;}}a98EventServer;
WiFiClient a98EventClient;bool a98EventServerStarted=true;
enum class Chip{A98,A31};struct Endpoint{Chip chip=Chip::A98;}activeEndpoint;String deviceIp="zone";
struct Subscription{String sid="uuid:valid";uint32_t seq=0;bool hasSeq=false;};Subscription a98Subscriptions[2];
char a98EventBuffer[18433];size_t a98EventLength=0,a98HeaderLength=0;int a98BodyLength=0,a98EventService=-1;
uint32_t a98ClientStarted=0,a98EventEpoch=5,a98IncomingSeq=0,now=100;uint32_t millis(){return now;}
a98::Model a98Model;int redraws=0;void a98ActiveChanged(int before){if(before!=a98Model.active)++redraws;}
'''+s[a:b]+r'''
std::shared_ptr<Socket> deliver(const std::string& body,const std::string& path="/a98/5/0",unsigned seq=0,const std::string& sid="uuid:valid",const std::string& ip="zone"){
 auto socket=std::make_shared<Socket>();socket->input="NOTIFY "+path+" HTTP/1.1\r\nSID: "+sid+"\r\nNT: upnp:event\r\nNTS: upnp:propchange\r\nSEQ: "+std::to_string(seq)+"\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;socket->ip=ip;a98EventServer.sockets.push_back(socket);for(int i=0;i<20&&(socket->connected||!a98EventServer.sockets.empty());++i)pollA98Events();return socket;
}
int main(){
 a98Model.keys[0].slot=1;a98Model.keys[0].fullName="Liked Music_#~2026-03-31 20:00:00";a98Model.keys[6].slot=7;a98Model.keys[6].input="optical";
 std::string media="<LastChange>&lt;Event&gt;&lt;CurretPlayListName val='Liked Music_#~2026-03-31 20:00:00'/&gt;&lt;TrackNums val='20'/&gt;&lt;/Event&gt;</LastChange>";
 auto socket=deliver(media);assert(socket->output.find("200 OK")!=std::string::npos&&a98Model.active==1);
 std::string input="<LastChange>&lt;Event&gt;&lt;CurrentTrackURI val='optical'/&gt;&lt;/Event&gt;</LastChange>";
 socket=deliver(input,"/a98/5/1");assert(a98Model.active==7&&socket->output.find("200 OK")!=std::string::npos);
 socket=deliver(media);assert(a98Model.active==7); // same SEQ is ignored
 socket=deliver(media,"/a98/4/0",1);assert(socket->output.find("412")!=std::string::npos&&a98Model.active==7);
 socket=deliver(media,"/a98/5/0",1,"uuid:old");assert(socket->output.find("412")!=std::string::npos&&a98Model.active==7);
 socket=deliver(media,"/a98/5/0",1,"uuid:valid","other-zone");assert(a98Model.active==7&&socket->output.empty());
 socket=deliver(media,"/a98/5/0",1);assert(a98Model.active==1);
 socket=deliver(std::string(17000,'x'),"/a98/5/0",2);assert(socket->output.find("400")!=std::string::npos);
 auto partial=std::make_shared<Socket>();partial->input="NOTIFY /a98/5/0 HTTP/1.1\r\nSID: uuid:valid\r\nNT: upnp:event\r\nNTS: upnp:propchange\r\nSEQ: 2\r\nContent-Length: 300\r\n\r\n<LastChange>";a98EventServer.sockets.push_back(partial);pollA98Events();assert(partial->connected&&a98Subscriptions[0].seq==1);now+=2100;pollA98Events();assert(!partial->connected&&a98Subscriptions[0].seq==1);
 std::cout<<"Actual NOTIFY receiver: ACKs, complete frames, SID/zone/epoch guards, duplicate SEQ, oversized bodies and stalled-frame timeout passed\n";
}
'''
Path('/tmp/a98_notify.cpp').write_text(code)
subprocess.run(['g++','-std=c++11','-Isrc','/tmp/a98_notify.cpp','-o','/tmp/a98_notify'],check=True)
subprocess.run(['/tmp/a98_notify'],check=True)

#include <string>
#include <array>
#define local static
#include "a98_presets.h"
#include <cassert>
#include <iostream>
std::string wrap(const std::string& event){std::string s;for(char c:event){if(c=='&')s+="&amp;";else if(c=='<')s+="&lt;";else if(c=='>')s+="&gt;";else s+=c;}return "<e:propertyset><e:property><LastChange>"+s+"</LastChange></e:property></e:propertyset>";}
int main(){
 a98::Model m;
 assert(m.mapping("<QueueContext><KeyList><MaxNumber>21</MaxNumber><Key0><RoutineId>Empty</RoutineId></Key0><Key1><Name>Liked Music_#~2026-03-31 20:00:00</Name></Key1><Key5><Name>Saffron and Smoke_#~2026-03-31 20:20:29</Name></Key5><Key7><RoutineId>routine-optical</RoutineId></Key7><Key8><RoutineId>routine-line</RoutineId></Key8><Key9><RoutineId>routine-bt</RoutineId></Key9><Key11><RoutineId>Empty</RoutineId></Key11><Key13><Name>Hidden</Name></Key13></KeyList></QueueContext>"));
 assert(m.keys[0].slot==1&&m.keys[0].label=="Liked Music"&&m.keys[6].routine=="routine-optical"&&m.keys[10].slot==0);
 m.keys[6].input="optical";m.keys[7].input="line-in";m.keys[8].input="bluetooth";
 m.event(true,wrap("<Event><InstanceID val='0'><CurretPlayListName val='Liked Music_#~2026-03-31 20:00:00'/><MusicSource val='YouTubeMusic'/><CurrentIndex val='1'/><TrackNums val='20'/></InstanceID></Event>"));assert(m.active==1);
 m.event(true,wrap("<Event><CurretPlayListName val='Saffron and Smoke_#~2026-03-31 20:20:29'/><MusicSource val='OnlineMusic'/><CurrentIndex val='1'/><TrackNums val='11'/></Event>"));assert(m.active==5);
 m.event(false,wrap("<Event><CurrentTrackURI val='optical'/><PlaybackStorageMedium val='OPTICAL'/></Event>"));assert(m.active==7&&m.inputActive);
 m.event(true,wrap("<Event><CurretPlayListName val='Saffron and Smoke_#~2026-03-31 20:20:29'/><TrackNums val='0'/></Event>"));assert(m.active==7);
 m.event(false,wrap("<Event><TransportState val='STOPPED'/></Event>"));assert(m.active==7);
 m.transport("LINE-IN","","");assert(m.active==8);m.transport("","BLUETOOTH","");assert(m.active==9);
 m.event(false,wrap("<Event><TransportState val='PAUSED_PLAYBACK'/></Event>"));assert(m.active==9);
 m.queue(m.keys[0].fullName,2);assert(m.active==1&&!m.inputActive);
 m.mode(43);assert(m.active==7);m.transport("https://radio.test/stream","","");assert(m.active==0&&!m.inputActive);
 m.keys[2].fullName=m.keys[0].fullName;m.keys[2].slot=3;m.queue(m.keys[0].fullName,2);assert(m.active==0); // ambiguous identity, no false highlight
 assert(a98::value("<Event><CurretPlayListName val=\"A &amp; B &quot;C&quot;\"/></Event>","CurretPlayListName",true)=="A & B \"C\"");
 assert(a98::unescape("&#x41;&#1050;")=="AК");
 a98::Notify h;
 std::string header="NOTIFY /a98/1/0 HTTP/1.1\r\nSID: uuid:test\r\nNT: upnp:event\r\nNTS: upnp:propchange\r\nSEQ: 0\r\nContent-Length: 123\r\n\r\n";
 assert(a98::parseNotify(header,h)&&h.length==123&&h.seq==0&&h.path=="/a98/1/0");
 assert(!a98::parseNotify("NOTIFY / HTTP/1.1\r\nContent-Length: 20000\r\n\r\n",h));
 assert(!a98::parseNotify("GET / HTTP/1.1\r\n\r\n",h));
 assert(!a98::newSequence(4,4)&&!a98::newSequence(3,4)&&a98::newSequence(1,0xffffffff));
 assert(!m.mapping("bad")&&m.active==0);
 std::cout<<"A98 canonical model: full keys21, routine inputs, exact timestamped playlists, external cross-type changes, stale queue/STOPPED, ambiguity, XML entities and NOTIFY bounds passed\n";
}

from pathlib import Path
import subprocess
source=Path('src/main.cpp').read_text()
function=source[source.index('bool detectEndpoint('):source.index('\n#include "wiim_worker_http.h"')]
assert 'WiFiClient probe' not in function
code=r'''
#include <string>
#include <vector>
#include <cassert>
#include <cstdint>
using String=std::string;
enum class Chip {Unknown,A31,A97,A98,A33,Linkplay};
struct Endpoint{Chip chip=Chip::Unknown;uint16_t port=80;bool tls=false;};
struct IPAddress{bool fromString(const String&s){return s=="192.168.0.2";}};
using JsonVariantConst=Chip;
struct DynamicJsonDocument{Chip chip;DynamicJsonDocument(int){} template<class T>T as(){return chip;}};
bool deserializeJson(DynamicJsonDocument&d,const String&s){if(s=="A31")d.chip=Chip::A31;else if(s=="A97")d.chip=Chip::A97;else if(s=="A98")d.chip=Chip::A98;else if(s=="A33")d.chip=Chip::A33;else if(s=="GENERIC")d.chip=Chip::Unknown;else return true;return false;}
Chip classifyChip(Chip c){return c;}
bool epochValid=true; bool discoveryEpochValid(uint32_t){return epochValid;}
bool looksLikeLinkplayStatus(Chip c){return c==Chip::Unknown;}
struct Logger{template<class...T>void printf(const char*,T...) {}} Serial;
void delay(int){}
std::vector<Endpoint> calls; uint16_t availablePort; String reply; bool failFirst=false;
bool readEndpoint(const String&,const Endpoint&e,const String&,String&body,uint32_t timeout){
 calls.push_back(e);if(failFirst&&calls.size()==1)return false;
 if(e.port!=availablePort)return false;
 assert((e.chip==Chip::A33)==(e.port==8000||e.port==8443));
 assert(e.tls==(e.port==443||e.port==8443));body=reply;return true;
}
'''+function+r'''
int main(){
 Endpoint result;String body;
 for(auto test:std::vector<std::pair<uint16_t,String>>{{80,"A31"},{443,"A97"},{443,"A98"},{8000,"A33"},{8443,"A33"}}){
 availablePort=test.first;reply=test.second;calls.clear();
 assert(detectEndpoint("192.168.0.2",result,body,availablePort));assert(calls.size()==1);assert(result.port==availablePort);
 }
 calls.clear();availablePort=80;reply="A31";failFirst=true;
 assert(detectEndpoint("192.168.0.2",result,body));assert(calls.size()==2);assert(result.chip==Chip::A31);failFirst=false;
 calls.clear();availablePort=8000;reply="A31";assert(!detectEndpoint("192.168.0.2",result,body,8000));
 calls.clear();availablePort=80;reply="GENERIC";assert(detectEndpoint("192.168.0.2",result,body));assert(result.chip==Chip::Linkplay);
 calls.clear();epochValid=false;assert(!detectEndpoint("192.168.0.2",result,body,80,600,1));assert(calls.empty());epochValid=true;
 calls.clear();availablePort=80;reply="invalid JSON";assert(!detectEndpoint("192.168.0.2",result,body));
}
'''
Path('/tmp/knob_discovery.cpp').write_text(code)
subprocess.run(['g++','-std=c++17','/tmp/knob_discovery.cpp','-o','/tmp/knob_discovery'],check=True)
subprocess.run(['/tmp/knob_discovery'],check=True)
print('Actual endpoint detector: all four chips, preferred ports, A31 retry and invalid status rejection passed')

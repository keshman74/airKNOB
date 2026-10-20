from pathlib import Path
import os, subprocess
s=Path('src/main.cpp').read_text()
# Compile the actual firmware parser against the same ArduinoJson library as PIO.
include=os.environ.get('ARDUINOJSON_TEST_INCLUDE','.pio/libdeps/onx2424g013/ArduinoJson/src')
assert Path(include,'ArduinoJson.h').exists(), 'ArduinoJson headers required; run pio run first'
mock=Path('tests/xml-media.test.py').read_text().split("code=r'''",1)[1].split("'''+s[a:b]",1)[0]
mock=mock.replace(' int indexOf(char c',' String(int n):std::string(std::to_string(n)){}\n bool isEmpty()const{return empty();}\n int indexOf(char c')
a=s.index('String decodeXml(');b=s.index('\nbool request(const String& command, String& response);',a)
xml=s[a:b]
a=s.index('String cleanPresetName(');b=s.index('bool parseKeyPresets(',a)
clean=s[a:b]
a=s.index('bool isWiiMPresetChip()');b=s.index('// A98 callback receiver',a)
parser=s[a:b]
a=s.index('  if(isWiiMPresetChip()) {',b);a=s.index('    StaticJsonDocument<256> filter;',a);b=s.index('\n  }',a)
filtercode=s[a:b]
code=mock+r'''
#include <ArduinoJson.h>
String presetReadError;
void convertFromJson(JsonVariantConst source,String& target){target=source.as<const char*>() ? source.as<const char*>() : "";}
bool canConvertFromJson(JsonVariantConst source,const String&){return source.is<const char*>();}
enum class Chip{A31,A97,A98,A33};struct Endpoint{Chip chip=Chip::A31;}activeEndpoint;
String lastCommand;int sends=0;bool request(const String& command,String&){lastCommand=command;++sends;return true;}
struct DevicePreset{int slot=0;String name;};DevicePreset devicePresets[20];int devicePresetCount=0;
'''+xml+clean+parser+'bool parseFixture(const char*body){devicePresetCount=0;\n'+filtercode+r'''
}
int main(){
 for(Chip chip:{Chip::A97,Chip::A98}){activeEndpoint.chip=chip;assert(sendWiiMPreset(11)&&lastCommand=="MCUKeyShortClick:11");assert(sendWiiMPreset(12)&&lastCommand=="MCUKeyShortClick:12");}
 int previous=sends;assert(!sendWiiMPreset(0)&&!sendWiiMPreset(13)&&sends==previous);
 for(Chip chip:{Chip::A31,Chip::A33}){activeEndpoint.chip=chip;assert(!sendWiiMPreset(1)&&sends==previous);}

 assert(parseFixture(R"({"preset_num":3,"preset_list":[{"number":12,"name":"Двенадцать","url":"secret"},{"number":1,"name":"BBC"},{"number":6,"name":"Radio Paradise"}]})"));
 assert(devicePresetCount==3&&devicePresets[0].slot==1&&devicePresets[1].slot==6&&devicePresets[2].slot==12);
 assert(devicePresets[2].name=="Двенадцать");
 assert(parseFixture(R"({"preset_list":[{"number":11,"name":"Eleven"},{"number":12,"name":"Twelve"},{"number":12,"name":"Duplicate"},{"number":0,"name":"Invalid"},{"number":13,"name":"Invalid"},{"number":"11","name":"Wrong type"},{"number":1.5,"name":"Wrong type"},{"number":3,"name":null},{"number":4,"name":" "}]})"));
 assert(devicePresetCount==2&&devicePresets[0].slot==11&&devicePresets[1].slot==12&&devicePresets[1].name=="Twelve");
 assert(parseFixture(R"({"preset_num":0,"preset_list":[]})")&&devicePresetCount==0);
 assert(!parseFixture(R"({"error":"unknown command"})")&&devicePresetCount==0);
 assert(!parseFixture("broken")&&devicePresetCount==0);
 assert(!parseFixture(R"({"preset_list":{}})")&&devicePresetCount==0);
 std::cout<<"Actual WiiM JSON parser/filter: sparse original IDs, sorted slots11/12, Cyrillic, duplicates, malformed/empty/type rejection passed\n";
}
'''

a=s.index('  if(isWiiMPresetChip()) {',s.index('bool readDevicePresets()'))
b=s.index('\n  }',a)
loader=s[a:b+4]
code=code.replace('struct Endpoint{Chip chip=Chip::A31;}','struct Endpoint{Chip chip=Chip::A31;unsigned port=80;bool tls=false;}')
helpers=r'''
String deviceIp="192.168.0.97",offered;int presetReadHttp=0,presetReadBytes=0,offeredHttp=200,reads=0;
unsigned lastPort=0,lastA31ActivityAt=0;bool lastTLS=false,receiveOK=true;
uint32_t millis(){return 1234;}
bool readEndpoint(const String& ip,const Endpoint& endpoint,const String& command,String& body,uint32_t timeout,int* status){
 assert(ip==deviceIp&&command=="getPresetInfo"&&timeout==1200);++reads;lastPort=endpoint.port;lastTLS=endpoint.tls;*status=offeredHttp;body=offered;return receiveOK;
}
bool loadFixture(){
'''+loader+r'''
return false;
}
'''
cases=r'''
 activeEndpoint.chip=Chip::A97;
 for(bool tls:{false,true}){
  activeEndpoint.tls=tls;activeEndpoint.port=tls?443:80;receiveOK=true;offeredHttp=200;
  offered=R"({"preset_list":[{"number":12,"name":"Twelve"},{"number":3,"name":"Люкс FM"}]})";
  assert(loadFixture()&&lastTLS==tls&&lastPort==(tls?443:80)&&lastA31ActivityAt==1234);
  assert(presetReadHttp==200&&presetReadBytes==int(offered.length())&&presetReadError.empty());
  assert(devicePresetCount==2&&devicePresets[0].slot==3&&devicePresets[1].slot==12);
 }
 receiveOK=false;offeredHttp=-1;offered="";assert(!loadFixture()&&presetReadHttp==-1&&presetReadBytes==0&&presetReadError=="http_or_body");
 receiveOK=true;offeredHttp=200;
 offered=R"({"preset_list":[)";assert(!loadFixture()&&presetReadError=="json_parse");
 // Filtered ArduinoJson may classify a non-object token as a structure error.
 offered="broken";assert(!loadFixture());
 offered=R"({"preset_list":{}})";assert(!loadFixture()&&presetReadError=="json_structure");
 offered=R"({"preset_list":[]})";assert(loadFixture()&&devicePresetCount==0&&presetReadError.empty());
'''
code=code.replace('int main(){',helpers+'int main(){'+cases,1)

Path('/tmp/knob_wiim_presets.cpp').write_text(code)
subprocess.run(['g++','-std=c++11','-I'+include,'/tmp/knob_wiim_presets.cpp','-o','/tmp/knob_wiim_presets'],check=True)
subprocess.run(['/tmp/knob_wiim_presets'],check=True)

from pathlib import Path
import os,subprocess
s=Path('src/main.cpp').read_text();a=s.index('bool readA98Presets() {');b=s.index('void maintainA98Subscriptions()',a)
mock=Path('tests/xml-media.test.py').read_text().split("code=r'''",1)[1].split("'''+s[a:b]",1)[0]
mock=mock.replace(' int indexOf(char c',' bool isEmpty()const{return empty();}\n int indexOf(char c')
code=mock+r'''
#include <ArduinoJson.h>
String presetReadError;
#include "a98_presets.h"
void convertFromJson(JsonVariantConst source,String& target){target=source.as<const char*>() ? source.as<const char*>() : "";}
bool canConvertFromJson(JsonVariantConst source,const String&){return source.is<const char*>();}
a98::Model a98Model;struct DevicePreset{int slot=0;String name;};DevicePreset devicePresets[20];int devicePresetCount=0;
String presetZone,a98Zone,deviceIp="zone",offeredMap,offeredRoutines;bool a98MetadataLoaded=false,mapOK=true,routineOK=true;
uint32_t lastA31ActivityAt=0,a98MetadataRetry=0;uint32_t millis(){return 1000;}
struct Logger{void println(const char*){}}Serial;void a98ActiveChanged(int){}
bool a98ReadKeyMapping(String& response){response=offeredMap;return mapOK;}
bool request(const String& command,String& response){assert(command=="getAllRoutines");response=offeredRoutines;return routineOK;}
'''+s[a:b]+r'''
int main(){
 offeredMap="<QueueContext><KeyList><MaxNumber>21</MaxNumber><Key1><RoutineId>r-optical</RoutineId></Key1><Key6><Name>Люкс FM_#~2026-10-13 01:00:00</Name></Key6><Key9><RoutineId>r-bt</RoutineId></Key9></KeyList></QueueContext>";
 offeredRoutines=R"({"routines":[{"id":"r-optical","index":1,"name":"Optical In","steps":[{"type":"audioInput","payload":{"input":"optical","secret":"ignored"}}]},{"id":"r-bt","index":9,"name":"Bluetooth","steps":[{"type":"audioInput","payload":{"input":"bluetooth"}}]}]})";
 assert(readA98Presets());assert(devicePresetCount==3&&devicePresets[0].slot==1&&devicePresets[0].name=="Optical In");
 assert(a98Model.keys[0].input=="optical"&&a98Model.keys[8].input=="bluetooth");a98Model.mode(43);assert(a98Model.active==1); // not hard-coded slot7
 assert(readA98Presets()&&a98Model.active==1);
 offeredRoutines=R"({"routines":[{"id":"r-optical","index":7,"name":"Wrong slot","steps":[{"type":"audioInput","payload":{"input":"optical"}}]}]})";
 assert(readA98Presets());assert(a98Model.keys[0].input.empty()&&devicePresets[0].name=="Routine 1");
 routineOK=false;assert(readA98Presets());assert(devicePresetCount==3&&a98Model.keys[0].input.empty());
 std::cout<<"Actual A98 routine loader: id/index joins, dynamic slot input labels, filtered JSON, preserved active state, unresolved/mismatched routine handling passed\n";
}
'''
Path('/tmp/a98_routines.cpp').write_text(code)
include=os.environ.get('ARDUINOJSON_TEST_INCLUDE','.pio/libdeps/onx2424g013/ArduinoJson/src')
subprocess.run(['g++','-std=c++11','-Isrc','-I'+include,'/tmp/a98_routines.cpp','-o','/tmp/a98_routines'],check=True)
subprocess.run(['/tmp/a98_routines'],check=True)

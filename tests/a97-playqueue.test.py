from pathlib import Path
import ast,os,subprocess
s=Path("src/main.cpp").read_text()
tree=ast.parse(Path("tests/presets.test.py").read_text())
mock=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=="mock" for t in n.targets))
mock=mock.replace(' int toInt()const',' String(int n):std::string(std::to_string(n)){} bool isEmpty()const{return empty();}\n int toInt()const')
a=s.index("String decodeXml(");b=s.index("\nbool request(const String& command, String& response);",a)
xml=s[a:b]
a=s.index("String cleanPresetName(");b=s.index("bool isWiiMPresetChip()",a)
parser=s[a:b]
a=s.index("bool isWiiMPresetChip()");b=s.index("bool sendWiiMPreset(",a)
chip=s[a:b]
a=s.index("bool parseWiiMPresets(");b=s.index("// A98 callback receiver",a)
wiim=s[a:b]
a=s.index("bool readKeyMappingPresets(");b=s.index("\nbool request(const String& command",a)
reader=s[a:b]
code=mock+r'''
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include <cstdint>
void convertFromJson(JsonVariantConst source,String& target){target=source.as<const char*>()?source.as<const char*>():"";}
bool canConvertFromJson(JsonVariantConst source,const String&){return source.is<const char*>();}
enum class Chip{A31,A33,A97,A98};struct Endpoint{Chip chip=Chip::A97;int port=443;bool tls=true;}activeEndpoint;
String deviceIp="192.168.0.99",presetZone,presetReadError,presetReadSource;
int presetReadHttp=0,presetReadBytes=0,presetReadPort=0,lastA31ActivityAt=0;
struct DevicePreset{int slot=0;String name;};DevicePreset devicePresets[20];int devicePresetCount=0;
struct Reply{int status=200;String body;bool complete=true;};
std::map<int,Reply> replies;std::vector<int> ports;int apiCalls=0,cloudCalls=0,a98Calls=0;bool apiOK=false;String apiBody;
int millis(){return 1234;}bool isCloud(){return activeEndpoint.chip==Chip::A33;}
struct WiFiClient{};
struct HTTPClient{
 int port=0;bool action=false;
 void useHTTP10(bool value){assert(value);}
 void setConnectTimeout(int n){assert(n==350);}void setTimeout(int n){assert(n==1200);}
 bool begin(WiFiClient&,const String& url){
  assert(url.find("http://192.168.0.99:")==0);
  port=url.find(":49152/")!=String::npos?49152:59152;
  assert(url.find("/upnp/control/PlayQueue1")!=String::npos);return true;
 }
 void addHeader(const String& key,const String& value){if(key=="SOAPAction"){assert(value=="\"urn:schemas-wiimu-com:service:PlayQueue:1#GetKeyMapping\"");action=true;}}
 int POST(const String& body){assert(action&&body.find("<u:GetKeyMapping")!=String::npos);ports.push_back(port);return replies[port].status;}
 void end(){}
};
bool readBoundedResponse(HTTPClient& http,String& body){body=replies[http.port].complete?replies[http.port].body:String("");return replies[http.port].complete;}
bool readEndpoint(const String& ip,const Endpoint& ep,const String& cmd,String& body,int timeout,int* status){
 assert(ip==deviceIp&&ep.tls&&ep.port==443&&cmd=="getPresetInfo"&&timeout==1200);
 ++apiCalls;*status=200;body=apiBody;return apiOK;
}
bool request(const String& cmd,String& body){assert(cmd=="getPresetListInfo");++cloudCalls;body=R"({"Presets":[{"PresetNumber":2,"PresetName":"Cloud preset"}]})";return true;}
bool readA98Presets(){++a98Calls;return true;}
'''+xml+parser+chip+wiim+reader+r'''
String mapXml(bool full=false){
 String body="<QueueContext>&lt;KeyList&gt;&lt;MaxNumber&gt;21&lt;/MaxNumber&gt;&lt;Key0&gt;&lt;Name&gt;reserved&lt;/Name&gt;&lt;/Key0&gt;";
 const char* names[]={"Nashe Radio Ukraine","Hit FM Ukraine","Luv 107.1","http://example.invalid/playlist.m3u","luxlviv","Local Radio"};
 for(int i=1;i<=21;++i){
  body+="&lt;Key"+std::to_string(i)+"&gt;";
  if(full||i<=6)body+="&lt;Name&gt;"+(full?std::string("slot")+std::to_string(i):std::string(names[i-1]))+(i<=3?"_#~2026-07-14 02:03:17":"")+"&lt;/Name&gt;";
  body+="&lt;/Key"+std::to_string(i)+"&gt;";
 }
 body+="&lt;/KeyList&gt;</QueueContext>";return body;
}
void reset(){ports.clear();apiCalls=cloudCalls=a98Calls=0;apiOK=false;apiBody="unknown command";activeEndpoint.chip=Chip::A97;replies[49152]={200,mapXml(),true};replies[59152]={200,mapXml(),true};}
int main(){
 reset();assert(readDevicePresets());assert((ports==std::vector<int>{49152})&&apiCalls==0);
 assert(devicePresetCount==6&&devicePresets[0].slot==1&&devicePresets[0].name=="Nashe Radio Ukraine"&&devicePresets[5].slot==6&&devicePresets[5].name=="Local Radio");
 assert(presetReadSource=="playqueue"&&presetReadPort==49152&&presetReadHttp==200&&presetReadError.empty());
 reset();replies[49152].status=404;assert(readDevicePresets());assert((ports==std::vector<int>{49152,59152})&&apiCalls==0&&presetReadPort==59152);
 reset();replies[49152].complete=false;assert(readDevicePresets()&&ports.size()==2&&presetReadPort==59152);
 reset();replies[49152].body="broken";assert(readDevicePresets()&&ports.size()==2);
 reset();replies[49152].body=mapXml(true);assert(readDevicePresets()&&devicePresetCount==12&&devicePresets[11].slot==12);
 reset();replies[49152].status=replies[59152].status=404;apiOK=true;apiBody=R"({"preset_list":[{"number":8,"name":"WiiM fallback"}]})";
 assert(readDevicePresets()&&apiCalls==1&&devicePresetCount==1&&devicePresets[0].slot==8&&presetReadSource=="getPresetInfo"&&presetReadPort==443);
 reset();replies[49152].status=replies[59152].status=404;assert(!readDevicePresets()&&devicePresetCount==0&&apiCalls==1);
 reset();activeEndpoint.chip=Chip::A31;replies[49152].body=mapXml(true);assert(readDevicePresets()&&devicePresetCount==20&&ports.size()==1&&apiCalls==0);
 reset();activeEndpoint.chip=Chip::A33;assert(readDevicePresets()&&ports.empty()&&apiCalls==0&&cloudCalls==1&&devicePresets[0].slot==2);
 reset();activeEndpoint.chip=Chip::A98;assert(readDevicePresets()&&ports.empty()&&apiCalls==0&&a98Calls==1);
 std::cout<<"Actual A97 PlayQueue reader: six keys, MaxNumber21, 49152/59152 fallback, bounds, HTTPS compatibility fallback and A31/A33/A98 isolation passed\n";
}
'''
Path("/tmp/a97_playqueue.cpp").write_text(code)
include=os.environ.get("ARDUINOJSON_TEST_INCLUDE",".pio/libdeps/onx2424g013/ArduinoJson/src")
subprocess.run(["g++","-std=c++17","-I"+include,"/tmp/a97_playqueue.cpp","-o","/tmp/a97_playqueue"],check=True)
subprocess.run(["/tmp/a97_playqueue"],check=True)

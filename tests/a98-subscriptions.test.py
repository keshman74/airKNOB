from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text();a=s.index('void maintainA98Subscriptions()');b=s.index('bool readKeyMappingPresets(',a)
mock=Path('tests/xml-media.test.py').read_text().split("code=r'''",1)[1].split("'''+s[a:b]",1)[0]
mock=mock.replace(' int indexOf(char c',' String(unsigned n):std::string(std::to_string(n)){}\n String(int n):std::string(std::to_string(n)){}\n int toInt()const{try{return std::stoi(*this);}catch(...){return 0;}}\n bool isEmpty()const{return empty();}\n int indexOf(char c')
code=mock+r'''
#include <vector>
#include <map>
#include <cstdint>
using std::min;
enum class Chip{A98,A31};struct Endpoint{Chip chip=Chip::A98;}activeEndpoint;
String a98Zone,deviceIp="zone1";bool volumeDirty=false,scanPhase=false,otaActive=false,localPending=false,a98MetadataLoaded=false,a98EventServerStarted=false;
uint32_t now=1000,a98MetadataRetry=0,a98EventEpoch=1,lastA31ActivityAt=0;uint32_t millis(){return now;}bool localVolumePending(){return localPending;}
struct Server{void begin(){}}a98EventServer;const int WL_CONNECTED=1;
struct IP{String toString(){return "knob";}};
struct Wifi{int status(){return WL_CONNECTED;}IP localIP(){return {};}}WiFi;
struct Sub{String sid;uint32_t due=0;bool hasSeq=false;};Sub a98Subscriptions[2];
struct Old{String ip,sid;int service=0;};Old a98OldSubscriptions[2];
const char* a98EventPaths[]={"/upnp/event/PlayQueue1","/upnp/event/rendertransport1"};
struct WiFiClient{};
int status=200,reads=0;std::vector<String> methods,urls;std::vector<std::map<String,String>> sentHeaders;
struct HTTPClient{String url;std::map<String,String> headers;void setConnectTimeout(int){}void setTimeout(int){}bool begin(WiFiClient&,String u){url=u;return true;}void collectHeaders(const char**,int){}void addHeader(String k,String v){headers[k]=v;}int sendRequest(String method){methods.push_back(method);urls.push_back(url);sentHeaders.push_back(headers);return status;}String header(const char*k){return String(k)=="SID" ? String("uuid:")+url : String("Second-600");}void end(){}};
void resetA98State(){for(int i=0;i<2;++i){a98OldSubscriptions[i].ip=a98Zone;a98OldSubscriptions[i].sid=a98Subscriptions[i].sid;a98OldSubscriptions[i].service=i;a98Subscriptions[i].sid="";a98Subscriptions[i].due=0;}a98Zone="";a98MetadataLoaded=false;a98MetadataRetry=0;++a98EventEpoch;}
bool readA98Presets(){++reads;a98MetadataLoaded=true;return true;}
'''+s[a:b]+r'''
int main(){
 maintainA98Subscriptions();assert(reads==1&&methods.empty());
 maintainA98Subscriptions();assert(methods.size()==1&&methods[0]=="SUBSCRIBE");assert(sentHeaders[0]["CALLBACK"]=="<http://knob:8081/a98/1/0>");
 maintainA98Subscriptions();assert(methods.size()==2&&sentHeaders[1]["CALLBACK"]=="<http://knob:8081/a98/1/1>");
 maintainA98Subscriptions();assert(methods.size()==2);
 now+=300001;maintainA98Subscriptions();assert(methods.size()==3&&sentHeaders[2].count("SID")&&!sentHeaders[2].count("CALLBACK"));
 volumeDirty=true;maintainA98Subscriptions();assert(methods.size()==3);volumeDirty=false;
 localPending=true;maintainA98Subscriptions();assert(methods.size()==3);localPending=false;
 status=412;maintainA98Subscriptions();assert(methods.size()==4&&a98Subscriptions[1].sid.empty());maintainA98Subscriptions();assert(methods.size()==4);status=200;
 deviceIp="zone2";maintainA98Subscriptions();assert(methods.back()=="UNSUBSCRIBE"&&urls.back().find("zone1:59152")!=std::string::npos);
 maintainA98Subscriptions();assert(reads==2);maintainA98Subscriptions();assert(sentHeaders.back()["CALLBACK"]=="<http://knob:8081/a98/2/0>");
 std::cout<<"Actual A98 subscription manager: two services, negotiated renewal, SID retry, idle/volume priority, old-zone unsubscribe and new epoch passed\n";
}
'''
Path('/tmp/a98_subscriptions.cpp').write_text(code)
subprocess.run(['g++','-std=c++11','/tmp/a98_subscriptions.cpp','-o','/tmp/a98_subscriptions'],check=True)
subprocess.run(['/tmp/a98_subscriptions'],check=True)

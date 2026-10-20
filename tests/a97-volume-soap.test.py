from pathlib import Path
import subprocess
s=Path('tests/xml-media.test.py').read_text()
mock=s.split("code=r'''",1)[1].split("'''+s[a:b]",1)[0]
mock=mock.replace(' int indexOf(char c',' String(int n):std::string(std::to_string(n)){}\n int indexOf(char c')
code=mock+r'''
#include <atomic>
#include <vector>
uint32_t now=10000;uint32_t millis(){return now;}
const int WL_CONNECTED=3;bool online=true,beginOK=true,complete=true;
struct {int status(){return online?WL_CONNECTED:0;}}WiFi;
struct IPAddress{bool fromString(const String&s){return s.find("192.168.")==0;}};
struct WiFiClient{};
int requests=0,status=200;String reply="<u:SetVolumeResponse xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\"/>";
std::vector<String> urls,bodies,actions;
struct HTTPClient{
 void useHTTP10(bool b){assert(b);}void setConnectTimeout(int n){assert(n==250);}
 void setTimeout(int n){assert(n==500);}
 bool begin(WiFiClient&,const String&url){urls.push_back(url);return beginOK;}
 void addHeader(const String&key,const String&value){if(key=="SOAPAction")actions.push_back(value);}
 int POST(const String&body){++requests;bodies.push_back(body);now+=15;return status;}
 void end(){}
};
bool readBoundedResponse(HTTPClient&,String&body,size_t limit,uint32_t budget){assert(limit==4096&&budget==1000);body=reply;return complete;}
#include "a97_volume_soap.h"
int main(){
 A97VolumeSoap soap;
 assert(soap.set("192.168.0.99",1,94));assert(soap.successes==1&&soap.lastHttp==200);
 assert(urls.back()=="http://192.168.0.99:49152/upnp/control/rendercontrol1");
 assert(actions.back()=="\"urn:schemas-upnp-org:service:RenderingControl:1#SetVolume\"");
 assert(bodies.back().find("<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredVolume>94</DesiredVolume>")!=String::npos);
 assert(soap.set("192.168.0.99",1,0));assert(soap.set("192.168.0.99",1,100));
 int before=requests;assert(!soap.set("192.168.0.99",1,-1));assert(!soap.set("192.168.0.99",1,101));assert(requests==before);
 // A successful HTTP code is insufficient: reject SOAP faults and wrong bodies.
 uint32_t epoch=2;
 for(auto bad:{"<s:Fault><faultcode>401</faultcode></s:Fault>",
               "<s:Envelope><s:Fault/><u:SetVolumeResponse/></s:Envelope>",
               "<NotSetVolumeResponse/>","<html>OK</html>",""}){
  reply=bad;assert(!soap.set("192.168.0.99",epoch++,30));
 }
 reply="<SetVolumeResponse/>";complete=false;assert(!soap.set("192.168.0.99",epoch++,30));complete=true;
 status=500;assert(!soap.set("192.168.0.99",epoch++,30));
 // Suppress repeated unsupported SOAP probes for 30s, allowing caller HTTPS fallback.
 before=requests;assert(!soap.set("192.168.0.99",epoch-1,32));assert(requests==before);
 now+=29999;assert(!soap.set("192.168.0.99",epoch-1,32));assert(requests==before);
 now+=1;status=200;assert(soap.set("192.168.0.99",epoch-1,32));assert(requests==before+1);
 // A zone/epoch change permits a fresh attempt, independently of prior failure.
 status=-1;assert(!soap.set("192.168.0.99",epoch,32));status=200;
 assert(soap.set("192.168.0.99",epoch+1,32));
 status=-1;assert(!soap.set("192.168.0.99",epoch+1,32));status=200;
 assert(soap.set("192.168.0.98",epoch+1,32));
 online=false;before=requests;assert(!soap.set("192.168.0.98",epoch+1,32));assert(requests==before);online=true;
 assert(!soap.set("bad",epoch+1,32));assert(requests==before);
 // Failed begin also backs off without posting an extra request.
 beginOK=false;assert(!soap.set("192.168.0.99",99,32));assert(requests==before);beginOK=true;
 assert(!soap.set("192.168.0.99",99,34));assert(requests==before);
 // Signed backoff comparisons remain valid across millis() wrap.
 now=0xfffffff0u;status=-1;assert(!soap.set("192.168.0.99",100,32));
 before=requests;now+=29999;status=200;assert(!soap.set("192.168.0.99",100,32));assert(requests==before);
 now+=1;assert(soap.set("192.168.0.99",100,32));assert(requests==before+1);
 std::cout<<"A97 SOAP: exact endpoint/action/arguments, levels, faults, bounded response, 30s fallback backoff, zone isolation and timer wrap passed\n";
}
'''
Path('/tmp/a97_volume_soap.cpp').write_text(code)
subprocess.run(['g++','-std=c++17','-Isrc','/tmp/a97_volume_soap.cpp','-o','/tmp/a97_volume_soap'],check=True)
subprocess.run(['/tmp/a97_volume_soap'],check=True)

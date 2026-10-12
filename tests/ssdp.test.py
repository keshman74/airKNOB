from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text();a=s.index('String ssdpLocationIp(');b=s.index('String ssdpSeen[',a)
code=r'''
#include <string>
#include <algorithm>
#include <regex>
#include <cassert>
struct String:std::string {
 using std::string::string; String(const std::string&s):std::string(s){} String(){}
 void toLowerCase(){std::transform(begin(),end(),begin(),[](unsigned char c){return std::tolower(c);});}
 int indexOf(const char*s){auto n=find(s);return n==npos?-1:int(n);}
 int indexOf(char c,int start)const{auto n=find(c,start);return n==npos?-1:int(n);}
 String substring(int a,int b)const{return substr(a,b-a);}
 bool startsWith(const char*s){return rfind(s,0)==0;}
 void trim(){auto a=find_first_not_of(" \t\r\n"),b=find_last_not_of(" \t\r\n");*this=a==npos?String(""):String(substr(a,b-a+1));}
};
struct IPAddress{bool fromString(const String&s){return std::regex_match(s,std::regex(R"(\d+\.\d+\.\d+\.\d+)"));}};
'''+s[a:b]+r'''
int main(){
 assert(ssdpLocationIp("HTTP/1.1 200 OK\r\nLOCATION: http://192.168.0.35:49152/description.xml\r\n\r\n")=="192.168.0.35");
 assert(ssdpLocationIp("NOTIFY * HTTP/1.1\r\nlocation: https://192.168.0.35/device.xml\r\n")=="192.168.0.35");
 assert(ssdpLocationIp("HTTP/1.1 200 OK\r\nLoCaTiOn:   http://192.168.0.35:49152/a\r\n")=="192.168.0.35");
 assert(ssdpLocationIp("HTTP/1.1 200 OK\r\nLOCATION: http://example.com/a\r\n").empty());
 assert(ssdpLocationIp("HTTP/1.1 200 OK\r\nLOCATION: file:///a\r\n").empty());
 assert(ssdpLocationIp("HTTP/1.1 200 OK\r\nST: ssdp:all\r\n").empty());
}
'''
Path('/tmp/knob_ssdp.cpp').write_text(code)
subprocess.run(['g++','-std=c++17','/tmp/knob_ssdp.cpp','-o','/tmp/knob_ssdp'],check=True)
subprocess.run(['/tmp/knob_ssdp'],check=True)
print('Actual SSDP LOCATION parser: response, NOTIFY, case, port and invalid URL tests passed')

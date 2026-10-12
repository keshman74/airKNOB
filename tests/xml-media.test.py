from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text();a=s.index('String decodeXml(');b=s.index('\nbool request(const String& command, String& response);',a)
code=r'''
#include <string>
#include <algorithm>
#include <cassert>
#include <iostream>
struct String:std::string {
 using std::string::string;String(const std::string&s):std::string(s){}String(){}
 int indexOf(char c,int start=0)const{auto n=find(c,start);return n==npos?-1:int(n);}
 int indexOf(const std::string&s,int start=0)const{auto n=find(s,start);return n==npos?-1:int(n);}
 int lastIndexOf(char c)const{auto n=rfind(c);return n==npos?-1:int(n);}
 String substring(int a,int b)const{return substr(a,b-a);}String substring(int a)const{return substr(a);}
 bool startsWith(const char*s)const{return rfind(s,0)==0;}
 bool endsWith(const char*s)const{size_t n=std::char_traits<char>::length(s);return size()>=n&&compare(size()-n,n,s)==0;}
 void trim(){auto a=find_first_not_of(" \t\r\n"),b=find_last_not_of(" \t\r\n");*this=a==npos?String(""):String(substr(a,b-a+1));}
 void replace(const char*from,const char*to){size_t n=0;while((n=find(from,n))!=npos){std::string::replace(n,std::char_traits<char>::length(from),to);n+=std::char_traits<char>::length(to);}}
};
'''+s[a:b]+r'''
int main(){
 String soap="<s:Envelope><u:TrackMetaData>&lt;DIDL-Lite&gt;&lt;upnp:albumArtURI&gt;http://test/logo.png?a=1&amp;amp;b=2&lt;/upnp:albumArtURI&gt;&lt;/DIDL-Lite&gt;</u:TrackMetaData></s:Envelope>";
 assert(decodeXml(xmlValue(decodeXml(xmlValue(soap,"TrackMetaData")),"albumArtURI"))=="http://test/logo.png?a=1&b=2");
 soap="<a:CurrentTrackMetaData><![CDATA[<DIDL-Lite><upnp:albumArtURI id='x'>https://test/art.jpg</upnp:albumArtURI></DIDL-Lite>]]></a:CurrentTrackMetaData>";
 assert(xmlValue(soap,"TrackMetaData").empty());
 assert(xmlValue(xmlValue(soap,"CurrentTrackMetaData"),"albumArtURI")=="https://test/art.jpg");
 assert(xmlValue("<DIDL x='albumArtURI'><upnp:albumArtURI>http://real/a</upnp:albumArtURI></DIDL>","albumArtURI")=="http://real/a");
 assert(xmlValue("<DIDL><upnp:albumArtURI>broken","albumArtURI").empty());
 std::cout<<"Actual SOAP metadata parser: namespace, DIDL, CDATA, entities, exact tags and truncated XML passed\n";
}
'''
Path('/tmp/knob_xml_media.cpp').write_text(code)
subprocess.run(['g++','-std=c++11','/tmp/knob_xml_media.cpp','-o','/tmp/knob_xml_media'],check=True)
subprocess.run(['/tmp/knob_xml_media'],check=True)

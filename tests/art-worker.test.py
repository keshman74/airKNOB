from pathlib import Path
import os,subprocess
s=Path('src/main.cpp').read_text()
mock=Path('tests/xml-media.test.py').read_text().split("code=r'''",1)[1].split("'''+s[a:b]",1)[0]
mock=mock.replace(' int indexOf(char c', ''' String(int n):std::string(std::to_string(n)){}
 bool isEmpty()const{return empty();}
 void toLowerCase(){for(char&c:*this)c=char(std::tolower((unsigned char)c));}
 bool equalsIgnoreCase(String other)const{String self=*this;self.toLowerCase();other.toLowerCase();return self==other;}
 int indexOf(char c''')
a=s.index('String jobArtworkUrl(');b=s.index('void commitArtworkJob()',a)
code=mock+r'''
#include <atomic>
#include <vector>
#include <ArduinoJson.h>
void convertFromJson(JsonVariantConst source,String& target){target=source.as<const char*>()?source.as<const char*>():"";}
bool canConvertFromJson(JsonVariantConst source,const String&){return source.is<const char*>();}
enum class Chip{A31,A33,A97,A98};struct Endpoint{Chip chip=Chip::A31;bool tls=false;int port=80;};
struct Job{bool ok=false,unchanged=false,hadArtwork=false;Endpoint endpoint;String body,reported,title,currentUrl,lastAttempt,ip="192.168.0.35";uint32_t lastAttemptAt=0;}backgroundPoll;
std::atomic<bool> artworkCancel{false};uint32_t millis(){return 10000;}
String decodeXml(String s){return s;}String cleanMedia(String s){s.trim();return s;}
std::vector<String> downloads;String offered,soap;int soapReads=0,metaReads=0;bool metaOK=true,decodeOK=true;
bool readEndpoint(const String&ip,const Endpoint&,const String&command,String&body,int){assert(ip==backgroundPoll.ip&&command=="getMetaInfo");++metaReads;body=offered;return metaOK;}
String fetchSoapArtworkAt(const String&ip,const Endpoint&){assert(ip==backgroundPoll.ip);++soapReads;return soap;}
bool downloadAndDecodeArt(const String&url){downloads.push_back(url);return decodeOK || url=="http://fallback";}
'''+s[a:b]+r'''
void reset(){backgroundPoll=Job{};downloads.clear();soapReads=metaReads=0;metaOK=decodeOK=true;artworkCancel.store(false);offered="";soap="http://station";}
int main(){
 reset();runArtworkJob();assert(metaReads==0&&soapReads==1&&backgroundPoll.ok&&downloads.size()==1);
 reset();decodeOK=false;runArtworkJob();assert(!backgroundPoll.ok&&soapReads==1); // no duplicate SOAP retry
 reset();backgroundPoll.hadArtwork=true;backgroundPoll.currentUrl=soap;runArtworkJob();assert(backgroundPoll.unchanged&&downloads.empty());
 reset();backgroundPoll.lastAttempt=soap;backgroundPoll.lastAttemptAt=9000;runArtworkJob();assert(backgroundPoll.unchanged&&downloads.empty());
 reset();backgroundPoll.endpoint.chip=Chip::A98;offered=R"({"metaData":{"albumArtURI":"https://art"}})";runArtworkJob();assert(metaReads==1&&soapReads==0&&backgroundPoll.ok);
 reset();backgroundPoll.endpoint.chip=Chip::A97;offered=R"({"metaData":{"albumArtURI":"https://bad"}})";decodeOK=false;soap="http://fallback";runArtworkJob();assert(metaReads==1&&soapReads==1&&downloads.size()==2&&backgroundPoll.ok&&backgroundPoll.body==soap);
 reset();backgroundPoll.endpoint.chip=Chip::A33;backgroundPoll.reported="https://cloud";runArtworkJob();assert(metaReads==0&&soapReads==0&&backgroundPoll.ok);
 reset();backgroundPoll.endpoint.chip=Chip::A33;backgroundPoll.title="new";offered=R"({"title":"old","albumArtURI":"https://old"})";runArtworkJob();assert(downloads.empty()&&!backgroundPoll.ok&&soapReads==0);
 reset();artworkCancel.store(true);runArtworkJob();assert(downloads.empty()&&metaReads==0&&soapReads==0);
 reset();backgroundPoll.endpoint.tls=true;backgroundPoll.endpoint.port=443;assert(jobArtworkUrl("/cover")=="https://192.168.0.35:443/cover");
 std::cout<<"Actual art worker: A31/A33/A97/A98 resolver, cached cover, retry bounds, no stale A33 art, cancellation and fallback passed\n";
}
'''
Path('/tmp/knob_art_worker.cpp').write_text(code)
include=os.environ.get('ARDUINOJSON_TEST_INCLUDE','.pio/libdeps/onx2424g013/ArduinoJson/src')
subprocess.run(['g++','-std=c++17','-Isrc','-I'+include,'/tmp/knob_art_worker.cpp','-o','/tmp/knob_art_worker'],check=True)
subprocess.run(['/tmp/knob_art_worker'],check=True)

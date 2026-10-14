#pragma once
#include <array>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
namespace a98 {
inline std::string lower(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s;}
inline std::string trim(std::string s){auto a=s.find_first_not_of(" \r\n\t"),b=s.find_last_not_of(" \r\n\t");return a==s.npos?"":s.substr(a,b-a+1);}
inline std::string unescape(const std::string& s){std::string out;for(size_t i=0;i<s.size();++i){if(s[i]!='&'){out+=s[i];continue;}auto end=s.find(';',i+1);if(end==s.npos||end-i>12){out+=s[i];continue;}auto e=s.substr(i+1,end-i-1);std::string v;
 if(e=="amp")v="&";else if(e=="lt")v="<";else if(e=="gt")v=">";else if(e=="quot")v="\"";else if(e=="apos")v="'";
 else if(e.size()>1&&e[0]=='#'){char* tail=nullptr;const bool hex=e.size()>2&&(e[1]=='x'||e[1]=='X');unsigned long n=std::strtoul(e.c_str()+(hex?2:1),&tail,hex?16:10);if(tail&&!*tail&&n>0&&n<=0x10ffff&&!(n>=0xd800&&n<=0xdfff)){if(n<128)v+=char(n);else if(n<2048){v+=char(0xc0|(n>>6));v+=char(0x80|(n&63));}else if(n<65536){v+=char(0xe0|(n>>12));v+=char(0x80|((n>>6)&63));v+=char(0x80|(n&63));}else{v+=char(0xf0|(n>>18));v+=char(0x80|((n>>12)&63));v+=char(0x80|((n>>6)&63));v+=char(0x80|(n&63));}}}
 if(v.empty())out+=s.substr(i,end-i+1);else out+=v;i=end;}return out;}
inline std::string local(const std::string& s){auto p=s.find(':');return p==s.npos?s:s.substr(p+1);}
inline std::string attribute(const std::string& tag,const std::string& key){size_t i=tag.find_first_of(" \t\r\n");while(i!=tag.npos&&i<tag.size()){while(i<tag.size()&&std::isspace((unsigned char)tag[i]))++i;size_t a=i;while(i<tag.size()&&!std::isspace((unsigned char)tag[i])&&tag[i]!='=')++i;auto name=tag.substr(a,i-a);while(i<tag.size()&&std::isspace((unsigned char)tag[i]))++i;if(i>=tag.size()||tag[i]!='=')break;++i;while(i<tag.size()&&std::isspace((unsigned char)tag[i]))++i;if(i>=tag.size()||(tag[i]!='\"'&&tag[i]!='\''))break;char q=tag[i++];a=i;i=tag.find(q,i);if(i==tag.npos)break;if(local(name)==key)return unescape(tag.substr(a,i-a));++i;}return "";}
inline std::string value(const std::string& xml,const std::string& key,bool event=false){size_t i=0;while((i=xml.find('<',i))!=xml.npos){size_t a=++i;if(i>=xml.size())break;if(xml[i]=='/'||xml[i]=='!'||xml[i]=='?')continue;auto end=xml.find('>',i);if(end==xml.npos)break;auto nameEnd=xml.find_first_of(" \t\r\n/>",i);auto name=xml.substr(a,nameEnd-a);if(local(name)!=key){i=end+1;continue;}auto opening=xml.substr(a,end-a);if(event){auto val=attribute(opening,"val");if(!val.empty()||opening.find("val=")!=opening.npos||opening.find("val =")!=opening.npos)return val;}
 if(end>0&&xml[end-1]=='/')return "";auto close=xml.find("</"+name+">",end+1);if(close==xml.npos)return "";auto raw=trim(xml.substr(end+1,close-end-1));if(raw.size()>=12&&raw.compare(0,9,"<![CDATA[")==0&&raw.substr(raw.size()-3)=="]]>")return raw.substr(9,raw.size()-12);return unescape(raw);}return "";}
inline std::string displayName(std::string s){auto pos=s.rfind("_#~");if(pos!=s.npos){auto t=s.substr(pos+3);if(t.size()==19&&t[4]=='-'&&t[7]=='-'&&t[10]==' '&&t[13]==':'&&t[16]==':')s.resize(pos);}return trim(s);}
inline std::string inputName(std::string s){s=lower(trim(s));if(s=="line_in"||s=="line in")s="line-in";return s=="optical"||s=="line-in"||s=="bluetooth"?s:"";}
struct Notify {std::string path,sid;int length=0;uint32_t seq=0;};
inline bool decimal(const std::string& s){return !s.empty() && std::all_of(s.begin(),s.end(),[](unsigned char c){return c>='0'&&c<='9';});}
inline bool parseNotify(const std::string& header,Notify& out){
 auto first=header.find("\r\n");if(first==header.npos||header.compare(0,7,"NOTIFY ")!=0)return false;auto end=header.find(' ',7);if(end==header.npos||header.substr(end+1,first-end-1)!="HTTP/1.1")return false;out.path=header.substr(7,end-7);
 std::string length,seq,nt,nts;size_t i=first+2;
 while(i<header.size()){auto e=header.find("\r\n",i);if(e==header.npos)break;if(e==i)break;auto colon=header.find(':',i);if(colon==header.npos||colon>=e)return false;auto key=lower(trim(header.substr(i,colon-i))),v=trim(header.substr(colon+1,e-colon-1));
 if(key=="content-length"){if(!length.empty())return false;length=v;}else if(key=="sid")out.sid=v;else if(key=="seq")seq=v;else if(key=="nt")nt=lower(v);else if(key=="nts")nts=lower(v);else if(key=="transfer-encoding")return false;i=e+2;}
 if(!decimal(length)||!decimal(seq)||out.sid.empty()||nt!="upnp:event"||nts!="upnp:propchange")return false;
 auto n=std::strtoul(length.c_str(),nullptr,10);auto q=std::strtoull(seq.c_str(),nullptr,10);if(n<1||n>16384||q>0xffffffffULL)return false;out.length=int(n);out.seq=uint32_t(q);return true;
}
inline bool newSequence(uint32_t next,uint32_t previous){return int32_t(next-previous)>0;}
struct Key{int slot=0;std::string fullName,label,routine,input;};
struct Model{
 std::array<Key,12> keys;int active=0;bool inputActive=false;std::string lastQueue;
 void clear(){keys={};active=0;inputActive=false;lastQueue.clear();}
 bool mapping(const std::string& response){clear();auto context=value(response,"QueueContext");if(context.find("<KeyList")==context.npos||context.find("</KeyList>")==context.npos)return false;int maximum=std::atoi(value(context,"MaxNumber").c_str());if(maximum<1||maximum>64)return false;
 for(int slot=1;slot<=std::min(maximum,12);++slot){auto entry=value(context,"Key"+std::to_string(slot));auto name=value(entry,"Name"),routine=trim(value(entry,"RoutineId"));if(lower(routine)=="empty")routine.clear();if(name.empty()&&routine.empty())continue;Key& k=keys[slot-1];k.slot=slot;k.fullName=name;k.label=displayName(name);k.routine=routine;if(k.label.empty())k.label="Routine "+std::to_string(slot);}return true;}
 int matchMedia(const std::string& name)const{int slot=0;for(const auto& k:keys)if(!name.empty()&&k.routine.empty()&&k.fullName==name){if(slot)return 0;slot=k.slot;}return slot;}
 int matchInput(const std::string& input)const{int slot=0;for(const auto& k:keys)if(!input.empty()&&k.input==input){if(slot)return 0;slot=k.slot;}return slot;}
 void queue(const std::string& name,int tracks){if(name.empty()||tracks==0)return;if(inputActive&&tracks<0&&name==lastQueue)return;active=matchMedia(name);inputActive=false;lastQueue=name;}
 void transport(const std::string& uri,const std::string& avt,const std::string& medium){auto input=inputName(uri);if(input.empty())input=inputName(avt);if(input.empty())input=inputName(medium);if(!input.empty()){active=matchInput(input);inputActive=true;return;}if(inputActive&&(lower(uri).find("http")==0||lower(avt).find("http")==0||lower(medium)=="network")){active=0;inputActive=false;}}
 void mode(int mode){const char* input=mode==43?"optical":mode==40?"line-in":mode==41?"bluetooth":nullptr;if(input){active=matchInput(input);inputActive=true;}}
 void event(bool playqueue,const std::string& body){auto xml=value(body,"LastChange");if(xml.empty())return;if(playqueue){auto count=value(xml,"TrackNums",true);queue(value(xml,"CurretPlayListName",true),count.empty()?-1:std::atoi(count.c_str()));}else transport(value(xml,"CurrentTrackURI",true),value(xml,"AVTransportURI",true),value(xml,"PlaybackStorageMedium",true));}
};
}

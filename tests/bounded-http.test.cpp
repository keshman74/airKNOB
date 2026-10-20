#include <string>
#include <cstdint>
#include <cassert>
#include <algorithm>
using std::size_t;
uint32_t now=0;uint32_t millis(){return now;}
struct String:std::string{using std::string::string;bool reserve(size_t){return true;}bool concat(const char*p,size_t n){append(p,n);return true;}};
struct Stream{virtual ~Stream(){};virtual int available()=0;virtual int read()=0;virtual int peek()=0;virtual void flush()=0;virtual size_t write(uint8_t)=0;virtual size_t write(const uint8_t*,size_t)=0;};
struct HTTPClient{int expected;std::string body;int elapsed=0,forcedError=0;int getSize(){return expected;}int writeToStream(Stream*s){int n=0;for(size_t i=0;i<body.size();i+=17){now+=elapsed;size_t count=std::min(size_t(17),body.size()-i);if(s->write((const uint8_t*)body.data()+i,count)!=count)return -10;n+=count;}return forcedError?forcedError:n;}};
#include "bounded_http.h"
int main(){String out="old";HTTPClient h{3,"abc"};assert(readBoundedResponse(h,out)&&out=="abc");h={4,"abc"};assert(!readBoundedResponse(h,out)&&out.empty());h={3,"abcdef"};assert(!readBoundedResponse(h,out)&&out.empty());h={70000,""};assert(!readBoundedResponse(h,out)&&out.empty());h={-1,std::string(100,'a')};assert(readBoundedResponse(h,out)&&out.size()==100);assert(!readBoundedResponse(h,out,50)&&out.empty());h={100,std::string(100,'a'),1000};assert(!readBoundedResponse(h,out,200,2500)&&out.empty());h={0,""};assert(readBoundedResponse(h,out)&&out.empty());h={3,"abc",0,-11};assert(!readBoundedResponse(h,out)&&out.empty());}

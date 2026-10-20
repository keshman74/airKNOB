from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text()
a=s.index('struct DiscoveryJob');b=s.index('bool initDiscoveryWorker()',a)
worker=s[a:b]
code=r'''
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <map>
#include <string>
#include <cstring>
#include <chrono>
#include <cassert>
#include <algorithm>
#include <iostream>
#include <sys/select.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cerrno>
#include "discovery_scheduler.h"
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0xffffffffu
using TaskHandle_t=void*;
struct String:std::string{using std::string::string;String(const std::string&s):std::string(s){}String(){}void toCharArray(char*out,size_t n)const{std::strncpy(out,c_str(),n);out[n-1]=0;}};
struct IPAddress{uint8_t bytes[4]={};IPAddress(){}IPAddress(uint32_t raw){for(int i=0;i<4;++i)bytes[i]=(raw>>(i*8))&255;}IPAddress(int a,int b,int c,int d){bytes[0]=a;bytes[1]=b;bytes[2]=c;bytes[3]=d;}uint8_t operator[](int i)const{return bytes[i];}String toString()const{return std::to_string(bytes[0])+"."+std::to_string(bytes[1])+"."+std::to_string(bytes[2])+"."+std::to_string(bytes[3]);}};
enum class Chip{Unknown,A31,A33};struct Endpoint{Chip chip=Chip::Unknown;uint16_t port=80;bool tls=false;};
struct Field{const char* text;Field operator|(Field other){return text?*this:other;}const char*operator|(const char*s){return text?text:s;}};
struct DynamicJsonDocument{DynamicJsonDocument(int){}Field operator[](const char*k){return Field{std::strcmp(k,"uuid")==0?"verified-uuid":"Zone"};}};
bool deserializeJson(DynamicJsonDocument&,const String&){return false;}
const char*chipName(Chip){return "A31";}String normalizedUuid(const String&s){return s;}
struct Queue{size_t size;std::mutex m;std::deque<std::vector<char>> rows;explicit Queue(size_t n):size(n){}};using QueueHandle_t=Queue*;
int xQueueSend(QueueHandle_t q,const void*p,uint32_t){std::lock_guard<std::mutex>l(q->m);if(q->rows.size()>=32)return 0;q->rows.emplace_back((const char*)p,(const char*)p+q->size);return 1;}
int xQueueReceive(QueueHandle_t q,void*p,uint32_t){std::lock_guard<std::mutex>l(q->m);if(q->rows.empty())return 0;std::memcpy(p,q->rows.front().data(),q->size);q->rows.pop_front();return 1;}
int uxQueueMessagesWaiting(QueueHandle_t q){std::lock_guard<std::mutex>l(q->m);return q->rows.size();}
struct Stop{};std::atomic<bool>quit{false},apiEntered{false};std::mutex wakeMutex;std::condition_variable wake;
uint32_t millis(){static auto start=std::chrono::steady_clock::now();return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();}
void vTaskDelay(uint32_t){if(quit.load())throw Stop{};std::this_thread::sleep_for(std::chrono::milliseconds(1));}
int ulTaskNotifyTake(int,uint32_t){std::unique_lock<std::mutex>l(wakeMutex);wake.wait_for(l,std::chrono::milliseconds(10));if(quit.load())throw Stop{};return 1;}
template<class T>T min(T a,T b){return std::min(a,b);}
std::map<int,int>fdHosts;int nextFd=10,live=0,maxLive=0;std::atomic<int>apiCalls{0};
int socket(int,int,int){int fd=nextFd++;if(fd>=FD_SETSIZE)fd=nextFd=10;fdHosts[fd]=0;++live;maxLive=std::max(maxLive,live);return fd;}
int fcntl(int,int,...){return 0;}
int connect(int fd,const sockaddr*addr,socklen_t){fdHosts[fd]=ntohl(((const sockaddr_in*)addr)->sin_addr.s_addr)&255;errno=EINPROGRESS;return -1;}
int close(int fd){assert(fdHosts.erase(fd)==1);--live;return 0;}
int select(int,fd_set*,fd_set*,fd_set*,timeval*){return 1;}
int getsockopt(int fd,int,int,void*out,socklen_t*){*(int*)out=fdHosts[fd]==35?0:ECONNREFUSED;return 0;}
bool detectEndpoint(const String&,Endpoint&e,String&body,uint16_t,uint32_t,uint32_t){assert(live==0);++apiCalls;apiEntered.store(true);std::this_thread::sleep_for(std::chrono::milliseconds(150));e.chip=Chip::A31;body="status";return true;}
'''+worker+r'''
int main(){
 Queue jobs(sizeof(DiscoveryJob)),results(sizeof(DiscoveryResult));discoveryJobs=&jobs;discoveryResults=&results;
 discoveryOwnIp.store(127u|(1u<<24));discoveryGeneration.store(1);discoveryRunning.store(true);
 DiscoveryJob first{1,35,80};xQueueSend(&jobs,&first,0);
 std::thread task([]{try{discoveryWorker(nullptr);}catch(Stop&) {}});
 while(!apiEntered.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
 // Main receiver can enqueue during a slow status request; no main-loop lock.
 for(int n=0;n<10;++n){xQueueSend(&jobs,&first,0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 discoveryRunning.store(false);discoveryGeneration.store(2);
 std::this_thread::sleep_for(std::chrono::milliseconds(180));assert(uxQueueMessagesWaiting(&results)==0);
 discoveryGeneration.store(3);discoveryRunning.store(true);DiscoveryJob second{3,35,80};xQueueSend(&jobs,&second,0);wake.notify_all();
 DiscoveryResult result{};const uint32_t deadline=millis()+1500;
 while(!xQueueReceive(&results,&result,0)&&millis()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 assert(result.generation==3&&result.ok&&result.host==35);
 // Let the actual worker exercise its raw probe batch; mock socket events are deterministic.
 std::this_thread::sleep_for(std::chrono::milliseconds(80));
 discoveryRunning.store(false);discoveryGeneration.fetch_add(1);std::this_thread::sleep_for(std::chrono::milliseconds(30));
 quit.store(true);wake.notify_all();task.join();assert(maxLive==4);assert(live==0);assert(apiCalls>=2);
 std::cout<<"Actual background worker: queue reception during slow API, stale result rejection, restart and four-socket bound passed\n";
}
'''
Path('/tmp/knob_worker.cpp').write_text(code)
subprocess.run(['g++','-std=c++11','-pthread','-I','src','/tmp/knob_worker.cpp','-o','/tmp/knob_worker'],check=True)
subprocess.run(['/tmp/knob_worker'],check=True,timeout=10)

from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text()
isr=s[s.index('volatile uint8_t encoderPrevious'):s.index('uint32_t feedbackUntil')]
batch=s[s.index('void pollEncoder() {'):s.index('\nvoid pollButton()')]
code=r'''
#include <algorithm>
#include <cstdint>
#include <cassert>
#include <string>
#define DRAM_ATTR
#define IRAM_ATTR
#define portMUX_INITIALIZER_UNLOCKED 0
using portMUX_TYPE=int;
#define portENTER_CRITICAL_ISR(x)
#define portEXIT_CRITICAL_ISR(x)
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
using std::max;
template<class T>T constrain(T n,T a,T b){return std::max(a,std::min(b,n));}
enum class MenuScreen{Player,Home,Scan,EqBandEdit,ToneEdit,DeviceInfo,NetworkInfo,MemoryInfo};MenuScreen menuScreen=MenuScreen::Player;
int volume=25;constexpr int kVolumePerDetent=2,kEncoderA=48,kEncoderB=47;
int pins=0;int digitalRead(int p){return p==48?(pins>>1)&1:pins&1;}
int selectedItem=0,editingBandValue=0,editingRaw=0,editingTone=0,infoLineCount=7,draws=0,volumeFailures=0,marqueeOffset=0;
uint32_t controlEpoch=0,volumeRetryAt=0,volumeChangedAt=0,marqueeStartedAt=0,marqueeLastAt=0;
bool volumeDirty=false,cloud=false;std::string toneMessage,menuMessage;
uint32_t millis(){return 1000;}bool isCloud(){return cloud;}int items=5;int menuCount(){return items;}
void recordInteraction(){}void drawScreen(){++draws;}void drawVolumeUpdate(){++draws;}void drawMenu(){++draws;}void drawInformation(){++draws;}
'''+isr+batch+r'''
void edge(int n){pins=n;captureEncoder();}
void detent(int direction,int count=1){for(int i=0;i<count;++i){edge(direction>0?2:1);edge(3);edge(direction>0?1:2);edge(0);}}
int main(){
 detent(-1,30);assert(encoderWrite==30);for(int n=0;n<30;++n)assert(encoderSteps[n]==-1);
 detent(1,30);assert(encoderWrite==60);for(int n=30;n<60;++n)assert(encoderSteps[n]==1);
 edge(1);edge(0);edge(1);edge(0);assert(encoderWrite==60);edge(3);edge(0);assert(encoderWrite==60);
 pollEncoder();assert(volume==60&&encoderRead==encoderWrite&&draws==1);
 volume=100;detent(1,10);detent(-1,2);pollEncoder();assert(volume==96&&encoderRead==encoderWrite&&draws==2);
 menuScreen=MenuScreen::Home;selectedItem=0;detent(1,20);detent(-1,2);pollEncoder();assert(selectedItem==2&&draws==3&&encoderRead==encoderWrite);
 items=0;detent(-1,4);pollEncoder();assert(selectedItem==0);detent(1,3);pollEncoder();assert(selectedItem==0);
 menuScreen=MenuScreen::ToneEdit;editingTone=1;editingRaw=4;detent(1,10);detent(-1,3);pollEncoder();assert(editingRaw==2);
 menuScreen=MenuScreen::EqBandEdit;cloud=true;editingBandValue=-12;detent(-1,5);detent(1,3);pollEncoder();assert(editingBandValue==-9);
 menuScreen=MenuScreen::MemoryInfo;selectedItem=0;detent(1,30);detent(-1);pollEncoder();assert(selectedItem==5);
 menuScreen=MenuScreen::Scan;detent(1,10);pollEncoder();assert(encoderRead==encoderWrite);
}
'''
Path('/tmp/knob_encoder.cpp').write_text(code)
subprocess.run(['g++','-std=c++17','/tmp/knob_encoder.cpp','-o','/tmp/knob_encoder'],check=True)
subprocess.run(['/tmp/knob_encoder'],check=True)
print('Actual ISR/UI: detents, bounce, limits, reversals, empty menu, tone/info and one redraw per batch passed')

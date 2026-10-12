from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text()
a=s.index('volatile uint8_t encoderPrevious');b=s.index('uint32_t feedbackUntil',a)
code='''#include <cstdint>
#include <cassert>
#define DRAM_ATTR
#define IRAM_ATTR
#define portMUX_INITIALIZER_UNLOCKED 0
using portMUX_TYPE = int;
#define portENTER_CRITICAL_ISR(x)
#define portEXIT_CRITICAL_ISR(x)
constexpr int kEncoderA=48,kEncoderB=47;
int pins=0;
int digitalRead(int p){return p==48 ? (pins>>1)&1 : pins&1;}
'''+s[a:b]+'''
void edge(int n){pins=n;captureEncoder();}
int main(){
for(int n=0;n<30;++n){edge(1);edge(3);edge(2);edge(0);}
assert(encoderWrite==30);for(int n=0;n<30;++n)assert(encoderSteps[n]==-1);
for(int n=0;n<30;++n){edge(2);edge(3);edge(1);edge(0);}
assert(encoderWrite==60);for(int n=30;n<60;++n)assert(encoderSteps[n]==1);
edge(1);edge(0);edge(1);edge(0);assert(encoderWrite==60);
edge(3);edge(0);assert(encoderWrite==60);
}
'''
Path('/tmp/knob_encoder.cpp').write_text(code)
subprocess.run(['g++','-std=c++17','/tmp/knob_encoder.cpp','-o','/tmp/knob_encoder'],check=True)
subprocess.run(['/tmp/knob_encoder'],check=True)
print('Actual ISR: 60 queued detents, reversal, bounce and invalid transitions passed')

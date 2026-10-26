from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text();a=s.index('class DisplayRouter {');b=s.index('\nDisplayRouter display;',a)
router=s[a:b]
a=s.index('void handleScreenshot() {');b=s.index('\nString htmlEscape(',a);screenshot=s[a:b]
code=r'''
#include <string>
#include <vector>
#include <memory>
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <iostream>
using std::min;using std::max;
struct String:std::string{using std::string::string;String(const std::string&s):std::string(s){}bool startsWith(const char*s)const{return rfind(s,0)==0;}};
int maxAllocation=0,liveAllocation=0,allocations=0;
struct Canvas{
 int w=240,h=240;std::vector<uint16_t> p;bool swap=false;
 Canvas():p(240*240){}void init(){}void setBrightness(int){}void setColorDepth(int){}
 bool createSprite(int width,int height){assert(height<=16);w=width;h=height;p.assign(w*h,0);maxAllocation=max(maxAllocation,w*h*2);liveAllocation=w*h*2;++allocations;return true;}
 void deleteSprite(){p.clear();liveAllocation=0;}
 uint16_t color565(int r,int g,int b){return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);}
 void fillScreen(uint16_t c){std::fill(p.begin(),p.end(),c);}
 void drawPixel(int x,int y,uint16_t c){if(x>=0&&y>=0&&x<w&&y<h)p[y*w+x]=c;}
 uint16_t readPixel(int x,int y){assert(x>=0&&y>=0&&x<w&&y<h);return p[y*w+x];}
 void fillRect(int x,int y,int width,int height,uint16_t c){for(int yy=max(y,0);yy<min(y+height,h);++yy)for(int xx=max(x,0);xx<min(x+width,w);++xx)drawPixel(xx,yy,c);}
 void fillRoundRect(int x,int y,int width,int height,int,uint16_t c){fillRect(x,y,width,height,c);}
 void fillCircle(int x,int y,int r,uint16_t c){for(int yy=y-r;yy<=y+r;++yy)for(int xx=x-r;xx<=x+r;++xx)if((xx-x)*(xx-x)+(yy-y)*(yy-y)<=r*r)drawPixel(xx,yy,c);}
 void drawLine(int x,int y,int x2,int y2,uint16_t c){int dx=abs(x2-x),sx=x<x2?1:-1,dy=-abs(y2-y),sy=y<y2?1:-1,e=dx+dy;for(;;){drawPixel(x,y,c);if(x==x2&&y==y2)break;int n=2*e;if(n>=dy){e+=dy;x+=sx;}if(n<=dx){e+=dx;y+=sy;}}}
 void pushImage(int x,int y,int width,int height,const uint16_t* pixels){for(int yy=0;yy<height;++yy)for(int xx=0;xx<width;++xx)drawPixel(x+xx,y+yy,pixels[yy*width+xx]);}
 void startWrite(){}void endWrite(){}void setTextDatum(int){}void setTextSize(int){}void setTextColor(int,int){}void setSwapBytes(bool v){swap=v;}
};
struct KnobDisplay:Canvas{};
namespace lgfx{struct LGFX_Sprite:Canvas{KnobDisplay* lcd;LGFX_Sprite(KnobDisplay*p):lcd(p){}void pushSprite(int x,int y){lcd->pushImage(x,y,w,h,p.data());}};}
'''+router+r'''
DisplayRouter display;
void drawScreen(){display.fillScreen(0);display.fillRect(7,9,210,43,0xf800);display.fillRoundRect(20,64,175,51,5,0x07e0);display.fillCircle(120,150,26,0x001f);display.drawLine(5,4,237,231,0xffff);display.drawPixel(29,201,0xffe0);uint16_t image[5*41];for(int i=0;i<205;++i)image[i]=uint16_t(i*97);display.pushImage(42,14,5,41,image);}
struct Sock{std::vector<uint8_t> output;bool connected=true;size_t limit=999999;};std::shared_ptr<Sock> offered;
struct WiFiClient{std::shared_ptr<Sock>s;explicit operator bool()const{return s&&s->connected;}void setTimeout(int){}String readStringUntil(char){return "GET /screenshot.bmp HTTP/1.1";}void print(const char*x){s->output.insert(s->output.end(),x,x+std::char_traits<char>::length(x));}size_t write(const uint8_t*p,size_t n){size_t amount=min(n,s->limit);s->output.insert(s->output.end(),p,p+amount);s->limit-=amount;return amount;}bool connected(){return s->connected;}void stop(){s->connected=false;}};
struct Server{WiFiClient available(){WiFiClient c;c.s=offered;return c;}}screenshotServer;void delay(int){}
'''+screenshot+r'''
int main(){
 display.init();drawScreen();
 std::vector<uint16_t> expected(240*240); // reference via ordinary direct rendering
 // Capture direct drawing in a full canvas with the same operations.
 Canvas reference;reference.fillScreen(0);reference.fillRect(7,9,210,43,0xf800);reference.fillRoundRect(20,64,175,51,5,0x07e0);reference.fillCircle(120,150,26,0x001f);reference.drawLine(5,4,237,231,0xffff);reference.drawPixel(29,201,0xffe0);uint16_t image[205];for(int i=0;i<205;++i)image[i]=uint16_t(i*97);reference.pushImage(42,14,5,41,image);
 for(int top=0;top<240;top+=16){assert(display.beginCapture(top));drawScreen();for(int y=top;y<top+16;++y)for(int x=0;x<240;++x)assert(display.capturePixel(x,y)==reference.p[y*240+x]);display.endCapture();}
 offered=std::make_shared<Sock>();handleScreenshot();auto& out=offered->output;std::string response(out.begin(),out.end());auto start=response.find("\r\n\r\n");assert(start!=std::string::npos);start+=4;assert(out.size()-start==172854&&out[start]=='B'&&out[start+1]=='M');
 for(int y=0;y<240;++y)for(int x=0;x<240;++x){auto c=reference.p[y*240+x];size_t i=start+54+(239-y)*720+x*3;assert(out[i]==(c&31)*255/31&&out[i+1]==((c>>5)&63)*255/63&&out[i+2]==((c>>11)&31)*255/31);}
 assert(maxAllocation==7680&&liveAllocation==0&&!display.isCapturing());
 offered=std::make_shared<Sock>();offered->limit=1000;handleScreenshot();assert(liveAllocation==0&&!display.isCapturing());
 std::cout<<"Actual DisplayRouter and screenshot: RGB565 strip equivalence, cross-boundary image/shape clipping, BMP row order/length/colors and disconnect cleanup; max buffer7680 bytes passed\n";
}
'''
Path('/tmp/knob_capture_bands.cpp').write_text(code)
subprocess.run(['g++','-std=c++11','/tmp/knob_capture_bands.cpp','-o','/tmp/knob_capture_bands'],check=True)
subprocess.run(['/tmp/knob_capture_bands'],check=True)

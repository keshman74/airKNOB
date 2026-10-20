from pathlib import Path
import subprocess
s=Path('src/main.cpp').read_text();callbacks=s[s.index('int pngLine('):s.index('\nString decodeXml')]
assert 'jpegDecoder.getJPEGType() == JPEG_MODE_PROGRESSIVE' in s
assert 'sourceWidth=(sourceWidth+7)/8;' in s
assert 'sourceHeight=(sourceHeight+7)/8;' in s
code=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <iostream>
using std::min;using std::max;
const int kArtSize=76,PNG_RGB565_LITTLE_ENDIAN=0;int sourceWidth,sourceHeight;uint16_t nextArtPixels[76*76];
struct PNGDRAW{int iWidth,y;};uint16_t value(int x,int y){return 1+(x*7+y*31)%60000;}
struct Decoder{void getLineAsRGB565(PNGDRAW*p,uint16_t*out,int,int){for(int x=0;x<p->iWidth;++x)out[x]=value(x,p->y);}}pngDecoder;
struct JPEGDRAW{int x,y,iWidth,iHeight;uint16_t*pPixels;};
'''+callbacks+r'''
void verify(){int side=max(sourceWidth,sourceHeight),w=max(1,sourceWidth*76/side),h=max(1,sourceHeight*76/side);
for(int y=0;y<76;++y)for(int x=0;x<76;++x){int dx=x-(76-w)/2,dy=y-(76-h)/2;
uint16_t expected=dx>=0&&dx<w&&dy>=0&&dy<h?value(dx*sourceWidth/w,dy*sourceHeight/h):0;assert(nextArtPixels[y*76+x]==expected);}}
int main(){for(auto size:std::vector<std::pair<int,int>>{{1,1},{8,8},{16,16},{32,24},{24,32},{75,75},{76,76},{77,79},{600,600},{1024,512},{2048,2048}}){
sourceWidth=size.first;sourceHeight=size.second;memset(nextArtPixels,0,sizeof(nextArtPixels));
for(int y=0;y<sourceHeight;++y){PNGDRAW p{sourceWidth,y};assert(pngLine(&p)==1);}verify();memset(nextArtPixels,0,sizeof(nextArtPixels));
for(int y=0;y<sourceHeight;y+=16)for(int x=0;x<sourceWidth;x+=16){int h=min(16,sourceHeight-y);uint16_t pixels[256];
for(int by=0;by<h;++by)for(int bx=0;bx<16;++bx)pixels[by*16+bx]=x+bx<sourceWidth?value(x+bx,y+by):65535;
JPEGDRAW p{x,y,16,h,pixels};assert(jpegBlock(&p)==1);}verify();}
std::cout<<"Actual PNG/JPEG callbacks: enlargement, shrinking, aspect ratio and padded MCU edges passed\n";}
'''
Path('/tmp/knob_art_scale.cpp').write_text(code)
subprocess.run(['g++','-std=c++11','/tmp/knob_art_scale.cpp','-o','/tmp/knob_art_scale'],check=True)
subprocess.run(['/tmp/knob_art_scale'],check=True)

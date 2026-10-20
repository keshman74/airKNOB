#include "volume_arc.h"
#include <cassert>
#include <iostream>
int main(){int pixels=0;for(int y=0;y<240;++y)for(int x=0;x<240;++x){auto level=volumeArcLevel(x,y);float dx=x-120.0f,dy=y-120.0f,d2=dx*dx+dy*dy;bool ring=d2>=107.f*107.f&&d2<=117.f*117.f;float angle=ring?atan2f(dy,dx)*180.f/3.1415926535897932384626433832795-160.f:360.f;if(angle<0)angle+=360.f;bool inside=x>=2&&x<239&&y>=2&&y<239&&ring&&angle<=220.f;assert((level!=255)==inside);if(inside){++pixels;for(int v=0;v<=100;++v)assert((v>=level)==(v>0&&angle<=220.f*v/100.f));}}std::cout<<pixels<<" arc pixels match original geometry and colours at all 101 volumes\n";}

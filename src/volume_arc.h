#pragma once
#include <cmath>
#include <cstdint>
// First integer volume at which a pixel is amber; 255 means outside the arc.
// Same geometry/comparison as the original rasterizer, cached once in PSRAM.
inline uint8_t volumeArcLevel(int x,int y) {
  if(x<2 || x>=239 || y<2 || y>=239) return 255;
  const float dx=x-120.0f,dy=y-120.0f,d2=dx*dx+dy*dy;
  if(d2<107.0f*107.0f || d2>117.0f*117.0f) return 255;
  float angle=atan2f(dy,dx)*180.0f/3.1415926535897932384626433832795-160.0f;
  if(angle<0) angle+=360.0f;
  if(angle>220.0f) return 255;
  int low=1,high=100;
  while(low<high){const int mid=(low+high)/2;if(angle<=220.0f*mid/100.0f) high=mid;else low=mid+1;}
  return uint8_t(low);
}

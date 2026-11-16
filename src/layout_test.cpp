#define NOMINMAX
#include "window_layout.hpp"
#include <cstdio>
int main() {
 RECT monitor{-1920,0,0,1080},work{-1920,0,0,1040};
 RECT full=ViewerRectangle(monitor,work,false,2,0);
 if(full.left!=-1920 || full.top!=0 || full.right!=0 || full.bottom!=1080)return 1;
 for(int size=0;size<4;++size)for(int position=0;position<5;++position){
  RECT r=ViewerRectangle(monitor,work,true,size,position);
  int width=r.right-r.left,height=r.bottom-r.top;
  if(width<=0 || height<=0 || r.left<work.left || r.right>work.right || r.top<work.top || r.bottom>work.bottom)return 2;
  if(abs(width*9-height*16)>16)return 3;
 }
 RECT tiny{0,0,200,100};auto fit=ViewerRectangle(tiny,tiny,true,3,4);
 if(fit.left<0 || fit.top<0 || fit.right>200 || fit.bottom>100)return 4;
 auto centered=ViewerRectangle(monitor,work,true,2,0);
 if(centered.right-centered.left!=1280 || centered.bottom-centered.top!=720 || centered.left!=-1600 || centered.top!=160)return 5;
 std::puts("PASS: fullscreen, negative monitor coordinates, all window sizes/positions, small-display fit");return 0;
}

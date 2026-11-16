#pragma once
#include <windows.h>
#include <algorithm>

inline RECT ViewerRectangle(RECT monitor,RECT work,bool windowed,int sizeIndex,int position) {
 if(!windowed) return monitor;
 static constexpr int widths[]={640,960,1280,1600};
 int width=widths[std::clamp(sizeIndex,0,3)],height=width*9/16;
 int availableWidth=std::max(1,int(work.right-work.left)-32);
 int availableHeight=std::max(1,int(work.bottom-work.top)-32);
 double scale=std::min({1.0,double(availableWidth)/width,double(availableHeight)/height});
 width=std::max(1,int(width*scale));height=std::max(1,int(height*scale));
 int x=work.left+(work.right-work.left-width)/2;
 int y=work.top+(work.bottom-work.top-height)/2;
 int padX=std::min(16,std::max(0,int(work.right-work.left-width)/2));
 int padY=std::min(16,std::max(0,int(work.bottom-work.top-height)/2));
 if(position==1 || position==3) x=work.left+padX;
 if(position==2 || position==4) x=work.right-padX-width;
 if(position==1 || position==2) y=work.top+padY;
 if(position==3 || position==4) y=work.bottom-padY-height;
 return {x,y,x+width,y+height};
}

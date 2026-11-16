// A controlled 32-bit D3D9 source to validate the same hook type TCS uses.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
 if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
 if (m == WM_CLOSE) { DestroyWindow(h); return 0; }
 if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
 return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR cmd,int) {
 WNDCLASSW wc{}; wc.hInstance=h; wc.lpfnWndProc=Proc; wc.lpszClassName=L"TCSCaptureFixture"; RegisterClassW(&wc);
 HWND w=CreateWindowExW(0,wc.lpszClassName,L"TCS Capture Fixture",WS_POPUP,40,40,640,360,nullptr,nullptr,h,nullptr);
 ShowWindow(w,SW_SHOWNOACTIVATE);
 if (wcsstr(cmd,L"--d3d11")) {
  DXGI_SWAP_CHAIN_DESC s{}; s.BufferDesc.Width=640; s.BufferDesc.Height=360; s.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
  s.SampleDesc.Count=1; s.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; s.BufferCount=2; s.OutputWindow=w; s.Windowed=TRUE; s.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
  ID3D11Device* d=nullptr; ID3D11DeviceContext* ctx=nullptr; IDXGISwapChain* swap=nullptr;
  HRESULT result=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&s,&swap,&d,nullptr,&ctx);
  if(FAILED(result))return 3;
  ID3D11Texture2D* back=nullptr; swap->GetBuffer(0,IID_PPV_ARGS(&back)); ID3D11RenderTargetView* target=nullptr; d->CreateRenderTargetView(back,nullptr,&target); back->Release();
  ULONGLONG begin=GetTickCount64(); unsigned f=0; MSG message{}; bool done=false;
  while(!done && GetTickCount64()-begin<45000) {
   while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)done=true;TranslateMessage(&message);DispatchMessageW(&message);}
   float color[4]={float((f*5)%256)/255,float((f*3)%256)/255,float((f*7)%256)/255,1};
   if(wcsstr(cmd,L"--gray")) color[0]=color[1]=color[2]=0.5f;
   ctx->ClearRenderTargetView(target,color); swap->Present(0,0); ++f; Sleep(25);
  }
  target->Release();swap->Release();ctx->Release();d->Release();if(IsWindow(w))DestroyWindow(w);return 0;
 }
 IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION); if(!api)return 1;
 D3DPRESENT_PARAMETERS p{}; p.Windowed=TRUE; p.SwapEffect=D3DSWAPEFFECT_DISCARD; p.hDeviceWindow=w;
 p.BackBufferWidth=640; p.BackBufferHeight=360; p.BackBufferFormat=D3DFMT_UNKNOWN; p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* device=nullptr;
 HRESULT hr=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,&p,&device);
 if(FAILED(hr)){ FILE* f=nullptr; fopen_s(&f,"fixture-error.txt","w"); if(f){fprintf(f,"CreateDevice HRESULT %08lx\n",hr); fclose(f);} api->Release();return 2;}
 ULONGLONG start=GetTickCount64(); unsigned frame=0; MSG msg{}; bool quit=false;
 while(!quit && GetTickCount64()-start<45000) {
  while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){ if(msg.message==WM_QUIT)quit=true; TranslateMessage(&msg); DispatchMessageW(&msg); }
  DWORD color = D3DCOLOR_XRGB((frame*5)%256,(frame*3)%256,(frame*7)%256);
  device->Clear(0,nullptr,D3DCLEAR_TARGET,color,1,0); device->BeginScene(); device->EndScene();
  hr=device->Present(nullptr,nullptr,nullptr,nullptr);
  if(hr==D3DERR_DEVICELOST && device->TestCooperativeLevel()==D3DERR_DEVICENOTRESET) device->Reset(&p);
  ++frame; Sleep(25);
 }
 device->Release();api->Release(); if(IsWindow(w))DestroyWindow(w); return 0;
}



#pragma once
#include <string>
#include <cstdint>
#include <cstring>

// Controlled reproduction of the typed shared textures used by D3D9 hooks.
// Only publishes mappings for this fixture process; no injection or hook signals.
struct TypedSharedFixture {
 ID3D11Texture2D* texture=nullptr;
 ID3D11RenderTargetView* target=nullptr;
 HANDLE keepalive=nullptr,infoMap=nullptr,dataMap=nullptr;
 void* info=nullptr;void* data=nullptr;
 bool Create(ID3D11Device* device,HWND window,bool bgrx) {
  D3D11_TEXTURE2D_DESC desc{};desc.Width=640;desc.Height=360;desc.MipLevels=desc.ArraySize=1;
  desc.Format=bgrx?DXGI_FORMAT_B8G8R8X8_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
  desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
  if(FAILED(device->CreateTexture2D(&desc,nullptr,&texture)) ||
     FAILED(device->CreateRenderTargetView(texture,nullptr,&target)))return false;
  IDXGIResource* resource=nullptr;HANDLE handle=nullptr;
  if(FAILED(texture->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(&resource))))return false;
  auto result=resource->GetSharedHandle(&handle);resource->Release();if(FAILED(result))return false;
  const auto pid=std::to_wstring(GetCurrentProcessId());
  keepalive=CreateMutexW(nullptr,FALSE,(L"CaptureHook_KeepAlive"+pid).c_str());
  infoMap=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,648,(L"CaptureHook_HookInfo"+pid).c_str());
  const auto name=L"CaptureHook_Texture_"+std::to_wstring(reinterpret_cast<uintptr_t>(window))+L"_1";
  dataMap=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,4,name.c_str());
  if(!keepalive || !infoMap || !dataMap)return false;
  info=MapViewOfFile(infoMap,FILE_MAP_WRITE,0,0,648);data=MapViewOfFile(dataMap,FILE_MAP_WRITE,0,0,4);
  if(!info || !data)return false;
  auto* fields=static_cast<uint32_t*>(info);std::memset(info,0,648);
  fields[0]=1;fields[1]=8;fields[2]=1;fields[3]=uint32_t(reinterpret_cast<uintptr_t>(window));
  fields[4]=desc.Format;fields[5]=640;fields[6]=360;fields[10]=1;fields[11]=4;
  *static_cast<uint32_t*>(data)=uint32_t(reinterpret_cast<uintptr_t>(handle));return true;
 }
 ~TypedSharedFixture(){
  if(data)UnmapViewOfFile(data);if(info)UnmapViewOfFile(info);
  if(dataMap)CloseHandle(dataMap);if(infoMap)CloseHandle(infoMap);if(keepalive)CloseHandle(keepalive);
  if(target)target->Release();if(texture)texture->Release();
 }
};

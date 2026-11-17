#pragma once
#include <cstddef>
#include <cstring>

// Read-only consumer of OBS's game hook protocol. Layout/names are from:
// obsproject/obs-studio shared/obs-hook-config/graphics-hook-info.h (32/33).
// Never inject, signal hook events, write hook settings, or hold its keepalive.
struct ObsHookPrefix {
 uint32_t major, minor, type, window, format, width, height;
 uint32_t unusedWidth, unusedHeight, pitch, mapId, mapSize;
 uint8_t flip;
 uint8_t padding[3];
};
static_assert(offsetof(ObsHookPrefix, mapId)==40 && offsetof(ObsHookPrefix, flip)==48);

inline bool ObsIsRunning() {
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
 if(snapshot==INVALID_HANDLE_VALUE) return false;
 PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
 if(Process32FirstW(snapshot,&entry))do {
  if(_wcsicmp(entry.szExeFile,L"obs64.exe")==0){found=true;break;}
 }while(Process32NextW(snapshot,&entry));
 CloseHandle(snapshot);return found;
}

class ObsSharedCapture {
 HANDLE infoMap=nullptr, dataMap=nullptr;
 const ObsHookPrefix* info=nullptr;
 const uint8_t* data=nullptr;
 HANDLE memoryMutex[2]{};
 DWORD pid=0;
 HWND window=nullptr;
 ObsHookPrefix current{};
 gs_texture_t* texture=nullptr;
 double retryAfter=0, aliveCheckAfter=0;
 bool alive=false;
 static std::wstring Name(const wchar_t* prefix, DWORD id){return std::wstring(prefix)+std::to_wstring(id);}
 void CloseData(ObsApi& api) {
  if(texture)api.gs_texture_destroy(texture);texture=nullptr;
  if(data)UnmapViewOfFile(data);data=nullptr;
  if(dataMap)CloseHandle(dataMap);dataMap=nullptr;
  for(auto& mutex:memoryMutex){if(mutex)CloseHandle(mutex);mutex=nullptr;}
  current={};
 }
public:
 uint64_t connections=0;
 void Configure(DWORD process,HWND target){pid=process;window=target;retryAfter=aliveCheckAfter=0;alive=false;connections=0;}
 void Reset(ObsApi& api) {
  CloseData(api);
  if(info)UnmapViewOfFile(info);info=nullptr;
  if(infoMap)CloseHandle(infoMap);infoMap=nullptr;
  alive=false;retryAfter=aliveCheckAfter=0;
 }
 static bool HookExists(DWORD process) {
  HANDLE mutex=OpenMutexW(SYNCHRONIZE,FALSE,Name(L"CaptureHook_KeepAlive",process).c_str());
  if(mutex)CloseHandle(mutex);
  return mutex!=nullptr;
 }
 gs_texture_t* Update(ObsApi& api,double now) {
  if(now>=aliveCheckAfter){alive=HookExists(pid);aliveCheckAfter=now+250;}
  if(!alive){if(infoMap)Reset(api);return nullptr;}
  if(!info) {
   if(now<retryAfter)return nullptr;
   retryAfter=now+250;
   infoMap=OpenFileMappingW(FILE_MAP_READ,FALSE,Name(L"CaptureHook_HookInfo",pid).c_str());
   if(!infoMap)return nullptr;
   info=static_cast<const ObsHookPrefix*>(MapViewOfFile(infoMap,FILE_MAP_READ,0,0,sizeof(ObsHookPrefix)));
   if(!info){CloseHandle(infoMap);infoMap=nullptr;return nullptr;}
  }
  ObsHookPrefix snapshot{};std::memcpy(&snapshot,info,sizeof(snapshot));
  if(snapshot.major==0)return nullptr;
  if(snapshot.major!=1)throw std::runtime_error("Unsupported OBS game-hook protocol. No hook settings were changed.");
  if(!snapshot.mapId || !snapshot.width || !snapshot.height)return nullptr;
  if(snapshot.width>16384 || snapshot.height>16384 || snapshot.mapSize>512u*1024u*1024u)
   throw std::runtime_error("Invalid OBS shared capture dimensions.");
  if(snapshot.mapId!=current.mapId || snapshot.width!=current.width || snapshot.height!=current.height ||
     snapshot.format!=current.format || snapshot.type!=current.type || snapshot.mapSize!=current.mapSize ||
     snapshot.window!=current.window || snapshot.pitch!=current.pitch) {
   if(now<retryAfter && current.mapId)return nullptr;
   CloseData(api);retryAfter=now+250;
   for(uint64_t captureWindow:{uint64_t(reinterpret_cast<uintptr_t>(window)),uint64_t(snapshot.window)}) {
    const auto name=L"CaptureHook_Texture_"+std::to_wstring(captureWindow)+L"_"+std::to_wstring(snapshot.mapId);
    dataMap=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());
    if(dataMap)break;
   }
   if(!dataMap)return nullptr;
   data=static_cast<const uint8_t*>(MapViewOfFile(dataMap,FILE_MAP_READ,0,0,snapshot.mapSize));
   if(!data){CloseData(api);return nullptr;}
   if(snapshot.type==1) {
    if(snapshot.mapSize<4){CloseData(api);return nullptr;}
    uint32_t handle;std::memcpy(&handle,data,sizeof(handle));
    if(handle)texture=api.gs_texture_open_shared(handle);
   } else if(snapshot.type==0) {
    gs_color_format format=GS_UNKNOWN;
    if(snapshot.format==DXGI_FORMAT_B8G8R8A8_UNORM || snapshot.format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)format=GS_BGRA;
    if(snapshot.format==DXGI_FORMAT_B8G8R8X8_UNORM || snapshot.format==DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)format=GS_BGRX;
    if(snapshot.format==DXGI_FORMAT_R8G8B8A8_UNORM || snapshot.format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)format=GS_RGBA;
    if(format==GS_UNKNOWN)throw std::runtime_error("OBS compatibility capture uses an unsupported pixel format. Disable multi-adapter compatibility in OBS Game Capture.");
    if(snapshot.mapSize<12 || snapshot.pitch<uint64_t(snapshot.width)*4)throw std::runtime_error("Invalid OBS shared-memory capture layout.");
    memoryMutex[0]=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,Name(L"CaptureHook_TextureMutex1",pid).c_str());
    memoryMutex[1]=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,Name(L"CaptureHook_TextureMutex2",pid).c_str());
    if(memoryMutex[0] && memoryMutex[1])texture=api.gs_texture_create(snapshot.width,snapshot.height,format,1,nullptr,GS_DYNAMIC);
   } else throw std::runtime_error("Unknown OBS shared capture type.");
   if(!texture){CloseData(api);return nullptr;}
   current=snapshot;++connections;
   Log("OBS shared capture attached (read-only): "+std::to_string(snapshot.width)+"x"+std::to_string(snapshot.height)+" map="+std::to_string(snapshot.mapId)+(snapshot.type==1?" shared texture":" shared memory"));
  }
  if(!texture)return nullptr;
  current.flip=snapshot.flip;
  if(current.type==0) {
   int32_t index;std::memcpy(&index,data,4);
   if(index<0 || index>1)return nullptr;
   DWORD locked=WaitForSingleObject(memoryMutex[index],0);
   if(locked!=WAIT_OBJECT_0 && locked!=WAIT_ABANDONED)return nullptr;
   uint32_t offset;std::memcpy(&offset,data+4+index*4,4);
   const uint64_t bytes=uint64_t(current.pitch)*current.height;
   if(offset>=12 && offset<=current.mapSize && bytes<=current.mapSize-offset)
    api.gs_texture_set_image(texture,data+offset,current.pitch,false);
   else {ReleaseMutex(memoryMutex[index]);throw std::runtime_error("Invalid OBS shared-memory frame bounds.");}
   ReleaseMutex(memoryMutex[index]);
  }
  return texture;
 }
 bool Flipped()const{return current.flip!=0;}
 bool OwnerAlive()const{return HookExists(pid);}
};
